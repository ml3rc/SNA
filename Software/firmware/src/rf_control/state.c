// State layer: current RF hardware configuration and the safe order of every
// hardware change, built only from the block layer.
#include "rf_control/state.h"

#include <stdatomic.h>

#include "pico/stdlib.h"

#include "frequency_limits.h"
#include "rf_control/ports.h"

#define RF_STATE_SOURCE_PWR_UP_REWRITE_DELAY 20

static atomic_uchar current_z = RF_STATE_DEFAULT_Z;
static atomic_uchar current_port = RF_STATE_DEFAULT_PORT;
static atomic_uchar current_atten = RF_STATE_PARKED_ATTEN; // requested code; on the hardware only while ON
static atomic_uchar source_state = RF_STATE_SOURCE_NONE;    // rf_state_source_t
static atomic_uchar current_lpf = RF_CTRL_LPF_NONE;
static atomic_uchar current_path = RF_CTRL_COUPLER_PORT1_FWD;
static atomic_bool source_cold; // MAX2871 CE went low->high, next tune needs the power-up double write
static uint64_t current_freq;   // written by the core that owns the RF hardware only

void rf_state_init(void) {
    rf_ctrl_source_init();
    rf_ctrl_coupler_init();
    rf_ctrl_detector_init();
    rf_ctrl_ports_init();

    atomic_store(&current_port, RF_STATE_DEFAULT_PORT);
    atomic_store(&current_atten, RF_STATE_PARKED_ATTEN);

    rf_state_set_source(RF_STATE_SOURCE_OFF); // defined levels before anything is powered

    rf_state_set_z(RF_STATE_DEFAULT_Z);
    rf_state_set_lpf_for_freq(SNA_FREQ_DEFAULT);
    rf_state_set_coupler_path(RF_CTRL_COUPLER_PORT1_FWD);

    // The old firmware first powered the MAX2871 at the start of the first
    // sweep, well over a second after boot; until then CE sat low with the
    // rails and the 100 MHz reference settled. Give it the same start: off,
    // wait, then power-up (amp first, then CE) and the double write below.
    sleep_ms(RF_STATE_SOURCE_BOOT_DELAY_MS);

    // The source is never turned off on its own, so its temperature (and
    // VCO/PLL behaviour) stays stable; only `manual src_en:false` does.
    rf_state_set_source(RF_STATE_SOURCE_PARKED);
    rf_state_set_freq(SNA_FREQ_DEFAULT);
}

static void source_power_up(void) {
    rf_ctrl_source_power(true);
    atomic_store(&source_cold, true); // see rf_state_set_freq()
}

bool rf_state_set_source(rf_state_source_t state) {
    rf_state_source_t previous = rf_state_get_source();
    if (previous == state) {
        return true;
    }
    switch (state) {
        case RF_STATE_SOURCE_OFF:
            // Attenuate, terminate, then power down.
            rf_ctrl_atten_write(RF_STATE_PARKED_ATTEN);
            rf_ctrl_port_switch_set(0);
            rf_ctrl_source_power(false);
            break;
        case RF_STATE_SOURCE_PARKED:
            // Attenuate first, then terminate: the amp can put out ~12-15 dBm
            // at 0 dB attenuation and the switch's internal termination must
            // not see that.
            rf_ctrl_atten_write(RF_STATE_PARKED_ATTEN);
            rf_ctrl_port_switch_set(0);
            if (previous == RF_STATE_SOURCE_OFF) source_power_up();
            break;
        case RF_STATE_SOURCE_ON:
            if (previous == RF_STATE_SOURCE_OFF) return false; // see state.h
            // The reverse of parking: route first, then release the attenuator.
            rf_ctrl_port_switch_set(rf_state_get_port());
            rf_ctrl_atten_write(rf_state_get_atten());
            break;
        default:
            return false;
    }
    atomic_store(&source_state, (unsigned char) state);
    return true;
}

rf_state_source_t rf_state_get_source(void) {
    return (rf_state_source_t) atomic_load(&source_state);
}

bool rf_state_set_port(uint8_t port) {
    if (port != 1 && port != 2) return false;
    atomic_store(&current_port, port); // remembered even when the source is not on
    if (rf_state_get_source() != RF_STATE_SOURCE_ON) return false;
    rf_ctrl_port_switch_set(port);
    return true;
}

uint8_t rf_state_get_port(void) {
    return atomic_load(&current_port);
}

bool rf_state_set_z(uint8_t z) {
    if (z != 50 && z != 75) return false;
    // Hot switching is allowed.
    rf_ctrl_z_switch_set(z);
    atomic_store(&current_z, z);
    return true;
}

uint8_t rf_state_get_z(void) {
    return atomic_load(&current_z);
}

void rf_state_set_atten(uint8_t value7bit) {
    if (value7bit > 127) value7bit = 127; // the HMC1119 has 7 bits; never wrap to a lower attenuation
    atomic_store(&current_atten, value7bit);
    if (rf_state_get_source() == RF_STATE_SOURCE_ON) rf_ctrl_atten_write(value7bit);
}

uint8_t rf_state_get_atten(void) {
    return atomic_load(&current_atten);
}

bool rf_state_set_freq(uint64_t freq_hz) {
    if (!rf_ctrl_source_set_freq(freq_hz)) return false;
    // Datasheet: after power-up (CE low->high) write the registers twice,
    // >=20 ms apart -- "the first write ensures the device is enabled, the
    // second starts VCO selection". Confirmed necessary on real hardware.
    // Done here, once per CE power-up, so every path that
    // switches the source on gets it, while ordinary retunes stay a single write.
    if (atomic_exchange(&source_cold, false)) {
        sleep_ms(RF_STATE_SOURCE_PWR_UP_REWRITE_DELAY);
        rf_ctrl_source_set_freq(freq_hz);
    }
    current_freq = freq_hz;
    return true;
}

uint64_t rf_state_get_freq(void) {
    return current_freq;
}

bool rf_state_source_locked(void) {
    return rf_ctrl_source_locked();
}

bool rf_state_read_temp_c(double *temp_c_out) {
    return rf_ctrl_source_read_temp_c(temp_c_out);
}

bool rf_state_set_lpf(uint8_t index) {
    if (!rf_ctrl_lpf_select(index)) return false;
    atomic_store(&current_lpf, index);
    return true;
}

void rf_state_set_lpf_for_freq(uint64_t freq_hz) {
    if (freq_hz <= RF_CTRL_LPF_FREQ_947M) rf_state_set_lpf(RF_CTRL_LPF_947M);
    else if (freq_hz <= RF_CTRL_LPF_FREQ_1747M) rf_state_set_lpf(RF_CTRL_LPF_1747M);
    else if (freq_hz <= RF_CTRL_LPF_FREQ_3500M) rf_state_set_lpf(RF_CTRL_LPF_3500M);
    else rf_state_set_lpf(RF_CTRL_LPF_NONE);
}

uint8_t rf_state_get_lpf(void) {
    return atomic_load(&current_lpf);
}

bool rf_state_set_coupler_path(rf_ctrl_coupler_path_t path) {
    if ((unsigned) path >= (unsigned) RF_CTRL_COUPLER_PATH_COUNT) return false;
    rf_ctrl_coupler_select(path);
    atomic_store(&current_path, (unsigned char) path);
    return true;
}

rf_ctrl_coupler_path_t rf_state_get_coupler_path(void) {
    return (rf_ctrl_coupler_path_t) atomic_load(&current_path);
}

rf_ctrl_detector_sample_t rf_state_detector_sample(void) {
    return rf_ctrl_detector_sample();
}
