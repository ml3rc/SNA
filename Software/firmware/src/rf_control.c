// Board-level RF composition layer: implements the application-facing API
// in rf_control.h (used by protocol.c) by driving the per-chip HAL modules
// in src/hal/. Each HAL module owns one IC's GPIO/register-level details;
// this file only knows how they're wired together on THIS board (which
// switch lines gang together for a port/impedance selection, what "safe
// startup state" means across all the RF blocks, etc).
#include "frequency_limits.h"

#include "rf_control.h"

#include "pico/stdlib.h"
#include "hardware/gpio.h"

#include "eeprom.h"
#include "main.h"
#include "protocol.h"
#include "tusb.h"
#include "hal/ad8318.h"
#include "hal/hmc1119.h"
#include "hal/max2871.h"
#include "hal/rf_switch.h"
#include "hal/trf37a73.h"

#include "sna.h"
#include <stdatomic.h>
#ifdef SNA_MULTICORE
#include "acquisition.h"
#include "pico/multicore.h"
#endif

static atomic_uchar current_z = 50;
static atomic_uchar current_port = 1;
static atomic_uchar current_atten = 127;
uint8_t rf_get_atten(void) { return atomic_load(&current_atten); }

// LPF bank: 2 switches (input and output of the filter bank). NOT driven
// with the same code -- see rf_set_lpf() below, confirmed by the user
// directly: the two switches use deliberately different legs per filter
// selection, chosen on the PCB layout to avoid crossing traces/vias.
static const rf_switch_t sw_lpf1 = { GPIO_SW_LPF1_C1, GPIO_SW_LPF1_C2 };
static const rf_switch_t sw_lpf2 = { GPIO_SW_LPF2_C1, GPIO_SW_LPF2_C2 };

// Directional coupler bank: 6 independently-addressed SP4T switches. Index
// <-> schematic designator, per Altium/SNAPCBVersion-INT's "RF Directional
// Coupler" sheet (GPIO_SW_CPLRn is named after the net label on that sheet,
// which matches these designators 1:1): 0=U502, 1=U501, 2=U503, 3=U504,
// 4=U506, 5=U505. See rf_select_coupler_path() below for the routing matrix
// driving these together (coupler1..6 in that function's table = this
// array's index 0..5, same order).
static const rf_switch_t sw_cplr[6] = {
    { GPIO_SW_CPLR1_C1, GPIO_SW_CPLR1_C2 },
    { GPIO_SW_CPLR2_C1, GPIO_SW_CPLR2_C2 },
    { GPIO_SW_CPLR3_C1, GPIO_SW_CPLR3_C2 },
    { GPIO_SW_CPLR4_C1, GPIO_SW_CPLR4_C2 },
    { GPIO_SW_CPLR5_C1, GPIO_SW_CPLR5_C2 },
    { GPIO_SW_CPLR6_C1, GPIO_SW_CPLR6_C2 },
};

// Each RFSW6042TR7's 2-bit code -> which of its 4 RF legs is selected,
// confirmed by the user directly (not in any datasheet we could find --
// Qorvo doesn't publish one for this part):
//   C1 C2 -> leg (pkg pin)
//    1  0 -> RF1 (9)
//    0  1 -> RF2 (11)
//    1  1 -> RF3 (6)
//    0  0 -> RF4 (4)
// rf_switch_set()'s `code` packs C1 in bit0, C2 in bit1, so:
#define RF_LEG_1 1 /* C1=1,C2=0 */
#define RF_LEG_2 2 /* C1=0,C2=1 */
#define RF_LEG_3 3 /* C1=1,C2=1 */
#define RF_LEG_4 0 /* C1=0,C2=0 */

// Port-select and 50/75ohm impedance switches: SPDT. See rf_set_port()/
// rf_set_z() below for the (user-confirmed) C1/C2 truth table -- NOT a
// "both bits same" gang, that was an earlier, wrong placeholder assumption.
static const rf_switch_t sw_port = { GPIO_SW_PORT_C1, GPIO_SW_PORT_C2 };
static const rf_switch_t sw_z = { GPIO_SW_Z_C1, GPIO_SW_Z_C2 };

void rf_control_init(void) {
    rf_switch_init(&sw_lpf1);
    rf_switch_init(&sw_lpf2);
    for (int i = 0; i < 6; i++) rf_switch_init(&sw_cplr[i]);
    rf_switch_init(&sw_port);
    rf_switch_init(&sw_z);
    // Bumped from the RP2350's 4mA reset default to the max 12mA -- at the
    // user's request, 2026-09-30. Only the port-select and 50/75ohm switch
    // lines; the LPF/coupler banks are left at the default.
    rf_switch_set_drive_strength(&sw_port, GPIO_DRIVE_STRENGTH_12MA);
    rf_switch_set_drive_strength(&sw_z, GPIO_DRIVE_STRENGTH_12MA);
    hmc1119_init();
    ad8318_init();
    trf37a73_init();
    max2871_init();

    rf_set_z(50);
    rf_set_port(1); // establish a known state -- previously left floating/undefined until a bench `manual` command happened to set it
    rf_set_atten(127); // start at max attenuation -- safest power-up state
    rf_detector_enable(false);
    rf_max2871_enable(false);
    rf_amp_enable(false); // amp powered down until a sweep actually needs it
}

void rf_set_atten(uint8_t value7bit) {
    hmc1119_set_atten(value7bit);
    atomic_store(&current_atten, value7bit);
}

void rf_set_lpf(uint8_t index) {
    // LPF bank: FL301 (3500MHz), FL302 (1747MHz), FL303 (947MHz), plus a
    // direct bypass ("none", e.g. for output frequencies above all three
    // cutoffs). Confirmed by the user directly, 2026-10-01 -- the two
    // switches are NOT set to the same leg for a given selection (the
    // original placeholder assumption, flagged unverified): they're
    // deliberately different, chosen on the PCB layout to avoid crossing
    // traces/vias where possible.
    switch (index & 0x3) {
        case 0: // none
            rf_switch_set(&sw_lpf1, RF_LEG_3);
            rf_switch_set(&sw_lpf2, RF_LEG_1);
            break;
        case 1: // 3500MHz
            rf_switch_set(&sw_lpf1, RF_LEG_4);
            rf_switch_set(&sw_lpf2, RF_LEG_2);
            break;
        case 2: // 1747MHz
            rf_switch_set(&sw_lpf1, RF_LEG_2);
            rf_switch_set(&sw_lpf2, RF_LEG_4);
            break;
        case 3: // 947MHz
            rf_switch_set(&sw_lpf1, RF_LEG_1);
            rf_switch_set(&sw_lpf2, RF_LEG_3);
            break;
    }
}

void rf_set_coupler_raw(uint16_t bits) {
    // Bench passthrough: independent control of the six 2-bit switches.
    for (int i = 0; i < 6; i++) {
        uint8_t code = (uint8_t) ((bits >> (i * 2)) & 0x3);
        rf_switch_set(&sw_cplr[i], code);
    }
}

// Coupler-bank routing matrix, supplied by the user directly, 2026-10-01 --
// this supersedes the earlier pixel-traced derivation of
// rf_select_sparam_path() (same six switches, but that function picked legs
// per S-parameter based on which physical port ended up on the coupler's
// INPUT vs OUTPUT side; this is the actual as-designed matrix, indexed by
// port + direction instead). All four of each switch's legs are genuinely
// used across this table -- none of them are a dead-end/bypass-only leg the
// way the old derivation assumed.
//
// The 50-ohm resistors at the switches (R510/R509/R530/R529 etc., the ones
// the old derivation read as "dead-end test points") are there for
// protection, not as an indication a leg is unused.
void rf_select_coupler_path(rf_coupler_path_t path) {
    // Row order matches rf_coupler_path_t; column order matches sw_cplr[]
    // (coupler1..6 = U502,U501,U503,U504,U506,U505, see the comment above
    // that array).
    static const uint8_t table[4][6] = {
        [RF_COUPLER_PORT1_FWD] = { RF_LEG_2, RF_LEG_4, RF_LEG_3, RF_LEG_2, RF_LEG_4, RF_LEG_1 },
        [RF_COUPLER_PORT1_BWD] = { RF_LEG_4, RF_LEG_4, RF_LEG_4, RF_LEG_1, RF_LEG_2, RF_LEG_1 },
        [RF_COUPLER_PORT2_FWD] = { RF_LEG_1, RF_LEG_2, RF_LEG_1, RF_LEG_3, RF_LEG_3, RF_LEG_2 },
        [RF_COUPLER_PORT2_BWD] = { RF_LEG_1, RF_LEG_1, RF_LEG_2, RF_LEG_4, RF_LEG_3, RF_LEG_4 },
    };
    for (int i = 0; i < 6; i++) {
        rf_switch_set(&sw_cplr[i], table[path][i]);
    }
}

void rf_set_port(uint8_t port) {
    // Confirmed by the user directly, 2026-10-01: NOT a "both bits same"
    // gang the way the original placeholder assumed -- a plain
    // opposite-bit SPDT select, same convention as rf_set_z() below.
    //   src -> port1: C1=0, C2=1 (code 0b10)
    //   src -> port2: C1=1, C2=0 (code 0b01)
    rf_switch_set(&sw_port, port == 1 ? 0b10 : 0b01);
    current_port = port;
    gpio_put(GPIO_LED_PORT1_OUT_EN, rf_get_port() == 1);
    gpio_put(GPIO_LED_PORT_Z50, rf_get_z() == 50);
}

uint8_t rf_get_port(void) {
    return current_port;
}

void rf_set_z(uint8_t z) {
    // Confirmed by the user directly, 2026-10-01 (same correction as
    // rf_set_port() above):
    //   z50: C1=1, C2=0 (code 0b01)
    //   z75: C1=0, C2=1 (code 0b10)
    bool is75 = (z == 75);
    rf_switch_set(&sw_z, is75 ? 0b10 : 0b01);
    current_z = z;
    gpio_put(GPIO_LED_PORT1_OUT_EN, rf_get_port() == 1);
    gpio_put(GPIO_LED_PORT_Z50, rf_get_z() == 50);
}

uint8_t rf_get_z(void) {
    return current_z;
}

void rf_detector_enable(bool en) {
    ad8318_enable(en);
}

uint16_t rf_detector_read_raw(void) {
    return ad8318_read_raw();
}

bool rf_detector_self_test(void) {
    ad8318_enable(false);
    sleep_ms(2);
    uint16_t off_raw = ad8318_read_raw();

    ad8318_enable(true);
    sleep_ms(2); // AD8318 datasheet log-amp settling is ~us-scale, well under this
    uint16_t on_raw = ad8318_read_raw();

    ad8318_enable(false); // leave detector powered down -- matches rf_control_init()'s safe startup state

    int32_t delta = (int32_t) on_raw - (int32_t) off_raw;
    if (delta < 0) delta = -delta;

    // Arbitrary but generous threshold: enabling the chip (no RF input
    // present) should move the pin off whatever it reads disabled/floating
    // by a lot more than ADC noise -- this doesn't assert a direction
    // because that depends on the board's pull network at DET_OUT, which
    // hasn't been traced.
    return delta > 50;
}

void rf_amp_enable(bool en) {
    trf37a73_enable(en);
}

bool rf_max2871_set_freq(uint64_t freq_hz) {
    if (freq_hz < SNA_FREQ_MIN_HZ || freq_hz > SNA_FREQ_MAX_HZ) return false;
    return max2871_set_freq(freq_hz);
}

void rf_max2871_enable(bool en) {
    max2871_enable(en);
}

bool rf_max2871_locked(void) {
    return max2871_locked();
}

uint8_t rf_max2871_read_die_id(void) {
    return max2871_read_die_id();
}

bool rf_max2871_read_temp_c(double *temp_c_out) {
    return max2871_read_temp_c(temp_c_out);
}

// --- Sweep/measurement session lifecycle ---

// Bounds, not fixed delays. Normal retunes finish as soon as LD stays high
// for 100 us; slower retunes get a 20 ms ceiling instead of the old 1 ms.
// Board lock-time characterization is still needed to tune these limits.
#define SOURCE_INITIAL_LOCK_TIMEOUT_US 2000000u
#define SOURCE_RETUNE_LOCK_TIMEOUT_US 20000u
#define SOURCE_LOCK_STABLE_US 100u
#define SOURCE_LOCK_POLL_US 50u
static uint32_t source_retune_timeout_us = SOURCE_RETUNE_LOCK_TIMEOUT_US;
static bool allow_unlocked;
void rf_set_allow_unlocked(bool allow) { allow_unlocked = allow; }
static rf_point_timing_t point_timing;
void rf_set_lock_timeout_us(uint32_t timeout_us) { source_retune_timeout_us = timeout_us; }
rf_point_timing_t rf_get_point_timing(void) { return point_timing; }

static bool wait_for_source_lock(uint32_t timeout_us) {
    uint64_t started = time_us_64();
    uint64_t high_since = 0;
    bool high = false;
    uint64_t last_service = started;
    while (time_us_64() - started <= timeout_us) {
        uint64_t now = time_us_64();
        if (now - last_service >= 1000) {
#ifdef SNA_MULTICORE
            if (get_core_num() == 1) {
                if (acquisition_cancelled()) return false;
            } else
#endif
            {
                tud_task();
                poll_usb_commands();
                if (protocol_measurement_cancelled()) return false;
            }
            last_service = time_us_64();
        }
        if (rf_max2871_locked()) {
            if (!high) {
                high_since = now;
                high = true;
            }
            if (now - high_since >= SOURCE_LOCK_STABLE_US) return true;
        } else {
            high = false;
        }
        sleep_us(SOURCE_LOCK_POLL_US);
    }
    return false;
}
static bool measurement_cancelled(void) {
#ifdef SNA_MULTICORE
    if (get_core_num() == 1) return acquisition_cancelled();
#endif
    return protocol_measurement_cancelled();
}
//
// A multi-point sweep must enable amp/detector/source ONCE and retune in
// place between points, not re-enable per point -- confirmed necessary on
// real hardware, 2026-10-01 (target: a 1001-point sweep in ~1-3s; doing a
// full CE power-cycle + cold-start lock-wait per point would blow that
// budget by well over an order of magnitude). The user also confirmed
// sweep frequencies are requested close together/"in blocks", not
// scattered across the whole range -- a retune to a nearby frequency is a
// small perturbation to an already-locked loop, very different from a
// cold power-up, which is what justifies the much shorter per-point
// settle budget in rf_measure_point() below.
bool rf_measure_session_start(uint8_t z, uint64_t first_freq_hz) {
    if (first_freq_hz < SNA_FREQ_MIN_HZ || first_freq_hz > SNA_FREQ_MAX_HZ ||
        (z != 50 && z != 75)) return false;
    rf_set_z(z);
    rf_amp_enable(true);
    rf_detector_enable(true);
    rf_max2871_enable(true);

    // Datasheet: registers must be written twice with >=20ms between
    // writes right after a true power-up (CE low->high) -- "the first
    // write enables the device, the second starts VCO selection" --
    // confirmed necessary on real hardware, 2026-10-01 (see main.c's
    // power_test_toggle()). This is the ONE place per sweep that pays that
    // cost; every subsequent retune in rf_measure_point() is a single
    // write against an already-enabled, already-biased chip.
    if (!rf_max2871_set_freq(first_freq_hz)) {
        rf_measure_session_end();
        return false;
    }
    sleep_ms(20);
    rf_max2871_set_freq(first_freq_hz);

    // Generous initial lock wait -- a one-time per-sweep cost, not
    // per-point, so erring generous (mirrors main.c's power_test_toggle())
    // has no real downside. TODO: exact bound still unverified for this
    // board's actual loop filter.
    if (!wait_for_source_lock(SOURCE_INITIAL_LOCK_TIMEOUT_US) &&
        (!allow_unlocked || measurement_cancelled())) {
        rf_measure_session_end();
        return false;
    }
    return true;
}

void rf_measure_session_end(void) {
    rf_amp_enable(false);
    rf_detector_enable(false);
    rf_max2871_enable(false);
}

// AD8318 output voltage per coupler-path reading, used below as both the
// reference and the numerator of an S-parameter ratio.
//
// The schematic specifies a 5 kHz detector output filter, so the previous
// 100 us delay could retain the previous path's voltage. Use a conservative
// 500 us before sampling (about 16 time constants for a 5 kHz one-pole model).
// Actual board settling still needs a hardware comparison. Median filtering
// below rejects ADC spikes; it does not replace this settling interval.
static void timed_port(uint8_t port) {
    uint64_t started = time_us_64();
    rf_set_port(port);
    point_timing.path_us += time_us_64() - started;
}
static void timed_path(rf_coupler_path_t path) {
    uint64_t started = time_us_64();
    rf_select_coupler_path(path);
    point_timing.path_us += time_us_64() - started;
}
static void timed_settle(void) {
    uint64_t started = time_us_64();
    sleep_us(RF_DETECTOR_SETTLE_US);
    point_timing.settle_us += time_us_64() - started;
}
static uint16_t timed_adc(void) {
    uint64_t started = time_us_64();
    uint16_t samples[RF_DETECTOR_SAMPLES];
    for (unsigned i = 0; i < RF_DETECTOR_SAMPLES; ++i) {
        uint16_t value = rf_detector_read_raw();
        unsigned j = i;
        while (j > 0 && samples[j - 1] > value) {
            samples[j] = samples[j - 1];
            --j;
        }
        samples[j] = value;
    }
    // Even sample count: round the average of the two middle ADC codes.
    uint16_t raw = (uint16_t)(((uint32_t)samples[RF_DETECTOR_SAMPLES / 2u - 1u] +
                              samples[RF_DETECTOR_SAMPLES / 2u] + 1u) / 2u);
    point_timing.adc_us += time_us_64() - started;
    point_timing.adc_reads += RF_DETECTOR_SAMPLES;
    return raw;
}
static uint16_t measure_volts(rf_coupler_path_t path) {
    timed_path(path);
    timed_settle();
    return timed_adc();
}

static rf_raw_measurement_t measure_point_impl(uint64_t freq_hz, uint8_t z,
                                   bool want_s11, bool want_s21,
                                   bool want_s12, bool want_s22) {
    rf_raw_measurement_t m = {0};

    if (freq_hz < SNA_FREQ_MIN_HZ || freq_hz > SNA_FREQ_MAX_HZ) return m;

    // Harmonic-suppression LPF: pick the lowest cutoff that still passes
    // freq_hz. TODO: this threshold scheme (use the next filter up once a
    // frequency exceeds the current one's cutoff) is a reasonable default,
    // not something the user confirmed -- revisit if measurements look odd
    // right at a filter boundary.
    uint64_t phase_started = time_us_64();
    if (freq_hz <= 947000000ULL) rf_set_lpf(3);
    else if (freq_hz <= 1747000000ULL) rf_set_lpf(2);
    else if (freq_hz <= 3500000000ULL) rf_set_lpf(1);
    else rf_set_lpf(0);
    point_timing.path_us += time_us_64() - phase_started;

    // Excitation and detector routing are independent. Sij uses source j.
    // A receiver reading is only reusable within the SAME source state:
    // port1 backward under source1 is reflection; under source2 it is transmission.
    if ((z != 50 && z != 75) || z != rf_get_z()) return m;
    phase_started = time_us_64();
    bool tuned = rf_max2871_set_freq(freq_hz);
    point_timing.tune_us = time_us_64() - phase_started;
    uint64_t retune_finished = time_us_64();
    if (!tuned) return m;
    bool need_source1 = want_s11 || want_s21;
    bool need_source2 = want_s12 || want_s22;
    if (!need_source1 && !need_source2) return m;

    // Start the source/coupler switch settling alongside the PLL retune.
    timed_port(need_source1 ? 1 : 2);
    timed_path(need_source1 ? RF_COUPLER_PORT1_FWD : RF_COUPLER_PORT2_FWD);
    timed_settle();
    phase_started = time_us_64();
    bool acquired = wait_for_source_lock(source_retune_timeout_us);
    point_timing.lock_wait_us = time_us_64() - phase_started;
    point_timing.lock_acquire_us = time_us_64() - retune_finished;
    if (measurement_cancelled() || (!acquired && !allow_unlocked)) return m;

    if (need_source1) {
        m.source1_ref = timed_adc();
        if (want_s11) m.source1_reflection = measure_volts(RF_COUPLER_PORT1_BWD);
        if (want_s21) m.source1_transmission = measure_volts(RF_COUPLER_PORT2_BWD);
    }
    if (need_source2) {
        if (need_source1) {
            timed_port(2);
            m.source2_ref = measure_volts(RF_COUPLER_PORT2_FWD);
        } else {
            m.source2_ref = timed_adc();
        }
        if (want_s22) m.source2_reflection = measure_volts(RF_COUPLER_PORT2_BWD);
        if (want_s12) m.source2_transmission = measure_volts(RF_COUPLER_PORT1_BWD);
    }
    m.valid = acquired;
    m.measured = true;

    return m;
}

rf_measurement_t rf_process_point(rf_raw_measurement_t raw, uint64_t freq_hz, uint8_t z,
                                 bool want_s11, bool want_s21, bool want_s12, bool want_s22) {
    rf_measurement_t m = {0};
    if (!raw.measured) return m;
    m.valid = raw.valid;
    m.measured = true;
    // AD8318 is already a LOG detector -- its output is linear in dB, not
    // linear in power -- so converting a voltage DIFFERENCE to a dB
    // difference is just dividing by the slope, no log/exp needed. Slope
    // used here (-24.5mV/dB) is the datasheet's typical value for
    // RTADJ=500ohm at 900MHz (Table "Logarithmic Slope and Intercept"),
    // which matches this board's actual TADJ resistor (R539=499ohm, see
    // the "RF Directional Coupler" schematic sheet) -- but the datasheet
    // only characterizes that exact RTADJ at 900MHz; this is applied
    // uniformly across the whole 50MHz-6000MHz range as an approximation,
    // not a frequency-corrected value.
    //
    // AD8318's Vout = intercept + slope*Pin_dBm (slope negative, so higher
    // input power means LOWER output voltage) -- so a power difference is
    // (V_numerator - V_reference)/slope, not the other way around. Checked
    // numerically, not just algebraically, after getting this backwards on
    // the first pass: a strong incident / weak reflected example (-10dBm
    // incident, -40dBm reflected, expecting S11=-30dB) only comes out
    // right with (num-ref)/slope.
    const double counts_per_db = -0.0245 * 4095.0 / 3.3;
    if (want_s11) m.s11_db = (raw.source1_reflection - raw.source1_ref) / counts_per_db;
    if (want_s22) m.s22_db = (raw.source2_reflection - raw.source2_ref) / counts_per_db;
    if (want_s21) m.s21_db = (raw.source1_transmission - raw.source1_ref) / counts_per_db;
    if (want_s12) m.s12_db = (raw.source2_transmission - raw.source2_ref) / counts_per_db;

    // EEPROM SLOT calibration: each of the 8 stored curves (s11_50/s21_50/
    // .../s22_75) is ALREADY the final correction value to subtract at a
    // given frequency -- what that curve actually contains (avg(open,short)
    // for reflection tracking, or the thru reading for transmission
    // tracking -- the standard scalar-NA techniques, confirmed against two
    // independent real scalar-analyzer references, 2026-10-01) is a client-
    // side cal-wizard concern; this function doesn't need to know how the
    // curve was derived, only that subtracting it removes the system's own
    // frequency response from the raw reading. `calibrated` is only true if
    // EVERY requested parameter had a stored curve covering this freq_hz --
    // if even one didn't, say false rather than imply the whole reading is
    // trustworthy when part of it is still raw.
    bool all_calibrated = want_s11 || want_s21 || want_s12 || want_s22;
    double corr;
    int z_off = (z == 75) ? 4 : 0;
    if (want_s11) {
        if (eeprom_interpolate_cached(z_off + 0, freq_hz, &corr)) m.s11_db -= corr;
        else all_calibrated = false;
    }
    if (want_s21) {
        if (eeprom_interpolate_cached(z_off + 1, freq_hz, &corr)) m.s21_db -= corr;
        else all_calibrated = false;
    }
    if (want_s12) {
        if (eeprom_interpolate_cached(z_off + 2, freq_hz, &corr)) m.s12_db -= corr;
        else all_calibrated = false;
    }
    if (want_s22) {
        if (eeprom_interpolate_cached(z_off + 3, freq_hz, &corr)) m.s22_db -= corr;
        else all_calibrated = false;
    }
    m.calibrated = all_calibrated && m.valid;

    return m;
}

rf_raw_measurement_t rf_acquire_point(uint64_t freq_hz, uint8_t z,
                                bool want_s11, bool want_s21,
                                bool want_s12, bool want_s22) {
    point_timing = (rf_point_timing_t){0};
    uint64_t started = time_us_64();
    rf_raw_measurement_t m = measure_point_impl(freq_hz, z, want_s11, want_s21, want_s12, want_s22);
    point_timing.total_us = time_us_64() - started;
    // Acquisition-only bookkeeping. The synchronous wrapper adds processing;
    // the dual-core consumer adds conversion/calibration on core 0 instead.
    point_timing.processing_us = point_timing.total_us - point_timing.tune_us -
        point_timing.path_us - point_timing.settle_us - point_timing.lock_wait_us - point_timing.adc_us;
    return m;
}

rf_measurement_t rf_measure_point(uint64_t f, uint8_t z,
                                bool s11, bool s21, bool s12, bool s22) {
    // Synchronous acquisition owns the hardware on this core. Populate the
    // cache before sampling; dual-core sweeps prefetch before launching work.
    eeprom_cal_prefetch();
    rf_raw_measurement_t raw = rf_acquire_point(f, z, s11, s21, s12, s22);
    uint64_t started = time_us_64();
    rf_measurement_t m = rf_process_point(raw, f, z, s11, s21, s12, s22);
    uint64_t elapsed = time_us_64() - started;
    point_timing.processing_us += elapsed;
    point_timing.total_us += elapsed;
    return m;
}
