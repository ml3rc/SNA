// Block layer: RF source chain (PLL, LPF bank, attenuator, amplifier).
#include "rf_control/source.h"

#include "frequency_limits.h"
#include "hal/hmc1119.h"
#include "hal/max2871.h"
#include "hal/rf_switch.h"
#include "hal/trf37a73.h"
#include "pico/stdlib.h"
#include "sna.h"

#define RF_CTRL_SOURCE_STARTUP_TIME 100

// LPF bank: 2 switches (input and output of the filter bank). NOT driven
// with the same code -- see rf_ctrl_lpf_select() below: the two switches
// use deliberately different legs per filter
// selection, chosen on the PCB layout to avoid crossing traces/vias.
static const hal_rf_switch_t sw_lpf1 = { GPIO_SW_LPF1_C1, GPIO_SW_LPF1_C2 };
static const hal_rf_switch_t sw_lpf2 = { GPIO_SW_LPF2_C1, GPIO_SW_LPF2_C2 };

void rf_ctrl_source_init(void) {
    hal_rf_switch_init(&sw_lpf1);
    hal_rf_switch_init(&sw_lpf2);
    hal_hmc1119_init();
    hal_trf37a73_init();
    hal_max2871_init();
}

void rf_ctrl_source_power(bool on) {
    // Same order as the old per-sweep start: amplifier up before the MAX2871's
    // CE/RFOUT_EN, and the MAX2871 down first.
    if (on) {
        hal_trf37a73_enable(true);
        hal_max2871_enable(true);
        sleep_ms(RF_CTRL_SOURCE_STARTUP_TIME);
    } else {
        hal_max2871_enable(false);
        hal_trf37a73_enable(false);
    }
}

void rf_ctrl_atten_write(uint8_t code) {
    if (code > RF_CTRL_ATTEN_MAX) code = RF_CTRL_ATTEN_MAX;
    hal_hmc1119_set_atten(code);
}

bool rf_ctrl_source_set_freq(uint64_t freq_hz) {
    if (freq_hz < SNA_FREQ_MIN_HZ || freq_hz > SNA_FREQ_MAX_HZ) return false;
    return hal_max2871_set_freq(freq_hz);
}

bool rf_ctrl_source_locked(void) {
    return hal_max2871_locked();
}

bool rf_ctrl_source_read_temp_c(double *temp_c_out) {
    return hal_max2871_read_temp_c(temp_c_out);
}

bool rf_ctrl_lpf_select(uint8_t index) {
    // LPF bank: FL301 (3500MHz), FL302 (1747MHz), FL303 (947MHz), plus a
    // direct bypass ("none", e.g. for output frequencies above all three
    // cutoffs).
    switch (index) {
        case RF_CTRL_LPF_NONE:
            hal_rf_switch_set(&sw_lpf1, HAL_RF_LEG_3);
            hal_rf_switch_set(&sw_lpf2, HAL_RF_LEG_1);
            return true;
        case RF_CTRL_LPF_3500M:
            hal_rf_switch_set(&sw_lpf1, HAL_RF_LEG_4);
            hal_rf_switch_set(&sw_lpf2, HAL_RF_LEG_2);
            return true;
        case RF_CTRL_LPF_1747M:
            hal_rf_switch_set(&sw_lpf1, HAL_RF_LEG_2);
            hal_rf_switch_set(&sw_lpf2, HAL_RF_LEG_4);
            return true;
        case RF_CTRL_LPF_947M:
            hal_rf_switch_set(&sw_lpf1, HAL_RF_LEG_1);
            hal_rf_switch_set(&sw_lpf2, HAL_RF_LEG_3);
            return true;
        default:
            return false; // only the four defined filter states exist
    }
}
