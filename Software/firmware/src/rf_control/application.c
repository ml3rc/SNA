// Application layer: the RF modes (IDLE / SWEEP / GENERATOR / PWR_DETECTOR /
// MANUAL, entered and left by the host) and the measurements, built only from
// the state layer. Runs on core 1 only (see application.h).
#include "rf_control/application.h"

#include <stddef.h>

#include "pico/stdlib.h"

#include "frequency_limits.h"
#include "rf_control/state.h"

// The application types mirror the block layer's, so they convert by cast.
_Static_assert((int) RF_APP_PATH_PORT1_FWD == (int) RF_CTRL_COUPLER_PORT1_FWD &&
               (int) RF_APP_PATH_PORT1_BWD == (int) RF_CTRL_COUPLER_PORT1_BWD &&
               (int) RF_APP_PATH_PORT2_FWD == (int) RF_CTRL_COUPLER_PORT2_FWD &&
               (int) RF_APP_PATH_PORT2_BWD == (int) RF_CTRL_COUPLER_PORT2_BWD &&
               (int) RF_APP_PATH_COUNT == (int) RF_CTRL_COUPLER_PATH_COUNT,
               "rf_app_path_t must match rf_ctrl_coupler_path_t");
_Static_assert(RF_APP_LPF_COUNT == RF_CTRL_LPF_947M + 1u, "RF_APP_LPF_COUNT must cover the LPF bank");

// AD8318 detector on the RP2350 ADC: datasheet typicals at 900 MHz with this
// board's RTADJ (see rf_app_counts_ratio_db()).
#define ADC_FULL_SCALE_COUNTS 4095.0
#define ADC_VREF_V 3.3
#define AD8318_SLOPE_V_PER_DB (-0.0245)
#define AD8318_INTERCEPT_DBM 20.0

// Bounds, not fixed delays. Normal retunes finish as soon as LD stays high
// for 500 us; slower retunes get a 20 ms ceiling.
// Board lock-time characterization is still needed to tune these limits.
#define SOURCE_INITIAL_LOCK_TIMEOUT_US 2000000u
#define SOURCE_RETUNE_LOCK_TIMEOUT_US 20000u
#define SOURCE_LOCK_STABLE_US 500u
#define SOURCE_LOCK_POLL_US 1u
#define CANCEL_POLL_US 1000u

static rf_app_hooks_t hooks; // set once by rf_app_init()

static rf_app_mode_t mode = RF_APP_IDLE;
static bool sweep_active;     // between rf_app_sweep_begin() and end()
static bool sweep_allow_unlocked; // setting of the running sweep session
static bool generator_on;     // the CW output is on
static bool power_running;    // continuous power measurement

static bool valid_z(uint8_t z) { return z == 50 || z == 75; }
static bool valid_port(uint8_t port) { return port == 1 || port == 2; }
static bool valid_freq(uint64_t f) { return f >= SNA_FREQ_MIN_HZ && f <= SNA_FREQ_MAX_HZ; }

// Abort, mode exit or host gone, as the code above this layer tells
// (rf_app_hooks_t). Without the hook nothing could stop a measurement, so it
// counts as cancelled (rf_app_init() reported RF_APP_INIT_ERR_NO_HOOKS).
static bool cancelled(void) {
    return hooks.cancelled == NULL || hooks.cancelled();
}

// An operation of `needed` may run only while that mode is entered.
static rf_app_result_t require_mode(rf_app_mode_t needed) {
    return mode == needed ? RF_APP_OK : RF_APP_ERR_WRONG_MODE;
}

// Waits until LD has stayed high for SOURCE_LOCK_STABLE_US. `cancellable`:
// gives up (false) once cancelled(), checked about every millisecond.
static bool wait_for_lock(uint32_t timeout_us, bool cancellable) {
    if (cancellable && cancelled()) return false; // also before a lock quicker than the first poll
    uint64_t started = time_us_64();
    uint64_t high_since = 0;
    bool high = false;
    uint64_t last_check = started;
    while (time_us_64() - started <= timeout_us) {
        uint64_t now = time_us_64();
        if (cancellable && now - last_check >= CANCEL_POLL_US) {
            if (cancelled()) return false;
            last_check = now;
        }
        if (rf_state_source_locked()) {
            if (!high) { high_since = now; high = true; }
            if (now - high_since >= SOURCE_LOCK_STABLE_US) return true;
        } else {
            high = false;
        }
        sleep_us(SOURCE_LOCK_POLL_US);
    }
    return false;
}

// --- Modes ---

uint8_t rf_app_init(const rf_app_hooks_t *app_hooks) {
    hooks = app_hooks != NULL ? *app_hooks : (rf_app_hooks_t) {0};
    rf_state_init(); // ends parked, tuned to SNA_FREQ_DEFAULT
    mode = RF_APP_IDLE;
    sweep_active = generator_on = power_running = false;
    if (hooks.cancelled == NULL) return RF_APP_INIT_ERR_NO_HOOKS;

    uint8_t errors = 0;
    if (!wait_for_lock(SOURCE_INITIAL_LOCK_TIMEOUT_US, false)) errors |= RF_APP_INIT_ERR_NOT_LOCKED;
    sleep_us(RF_CTRL_DETECTOR_SETTLE_US); // the coupler path was just selected
    uint16_t parked = rf_state_detector_sample().median;
    if (parked < RF_APP_DETECTOR_PARKED_MIN || parked > RF_APP_DETECTOR_PARKED_MAX)
        errors |= RF_APP_INIT_ERR_DETECTOR;
    return errors;
}

rf_app_mode_t rf_app_get_mode(void) { return mode; }

rf_app_result_t rf_app_mode_enter(rf_app_mode_t next) {
    switch (next) {
        case RF_APP_SWEEP:
        case RF_APP_GENERATOR:
        case RF_APP_PWR_DETECTOR:
        case RF_APP_MANUAL:
            break;
        default:
            return RF_APP_ERR_BAD_ARGS;
    }
    if (mode != RF_APP_IDLE) return RF_APP_ERR_MODE_ACTIVE;
    mode = next;
    return RF_APP_OK;
}

rf_app_result_t rf_app_mode_exit(void) {
    if (sweep_active) return RF_APP_ERR_BUSY; // the caller ends the session first
    // IDLE is always the same hardware state: source running and parked (also
    // after `manual src_en:false`), generator and power measurement off.
    generator_on = false;
    power_running = false;
    rf_state_set_source(RF_STATE_SOURCE_PARKED);
    mode = RF_APP_IDLE;
    return RF_APP_OK;
}

// --- Settings and status ---

uint8_t rf_app_get_z(void) { return rf_state_get_z(); }
uint8_t rf_app_get_port(void) { return rf_state_get_port(); }
uint8_t rf_app_get_atten(void) { return rf_state_get_atten(); }
bool rf_app_source_on(void) { return rf_state_get_source() == RF_STATE_SOURCE_ON; }
bool rf_app_sweep_active(void) { return sweep_active; }
bool rf_app_generator_on(void) { return generator_on; }
// Nothing else can retune the source while the generator output is on (other modes are blocked).
uint64_t rf_app_generator_freq(void) { return rf_state_get_freq(); }
bool rf_app_power_running(void) { return power_running; }
// Nothing else can move the coupler path in PWR_DETECTOR, so it tells the port.
uint8_t rf_app_power_port(void) {
    return rf_state_get_coupler_path() == RF_CTRL_COUPLER_PORT1_BWD ? 1 : 2;
}

rf_app_result_t rf_app_set_z(uint8_t z) {
    if (!valid_z(z)) return RF_APP_ERR_BAD_ARGS;
    rf_state_set_z(z); // hot switching does not harm the hardware
    return RF_APP_OK;
}

rf_app_result_t rf_app_set_atten(uint8_t atten) {
    if (atten > 127) return RF_APP_ERR_BAD_ARGS;
    // Reaches the HMC1119 only while the source is on, otherwise when it is switched on.
    rf_state_set_atten(atten);
    return RF_APP_OK;
}

// --- SWEEP ---

// A multi-point sweep enables amp/detector/source ONCE and retunes in place
// between points, not re-enable per point.
bool rf_app_sweep_begin(uint8_t z, uint64_t first_freq_hz, bool allow_unlocked_points, uint8_t atten) {
    if (!valid_freq(first_freq_hz) || !valid_z(z) || atten > 127) return false;
    if (require_mode(RF_APP_SWEEP) != RF_APP_OK || sweep_active) return false;
    sweep_active = true;
    rf_state_set_atten(atten); // stored while parked, applied when the source is on; stays the setting afterwards
    sweep_allow_unlocked = allow_unlocked_points;

    // Keep the DUT/coupler matrix isolated while the source (re)locks; the
    // source is switched on per point in start_point().
    rf_state_set_source(RF_STATE_SOURCE_PARKED);
    rf_state_set_z(z);

    // Does the datasheet's power-up double write if the source was off; the
    // source is left running between sweeps (see rf_app_sweep_end()), so
    // normally this is a single write against an already-running chip.
    if (!rf_state_set_freq(first_freq_hz)) return false; // still active until rf_app_sweep_end()

    // Generous initial lock wait -- a one-time per-sweep cost, not
    // per-point, so erring generous has no real downside. TODO: exact bound
    // still unverified for this board's actual loop filter.
    if (!wait_for_lock(SOURCE_INITIAL_LOCK_TIMEOUT_US, true) && (!sweep_allow_unlocked || cancelled()))
        return false;
    return true;
}

void rf_app_sweep_end(void) {
    if (!sweep_active) return;
    // Terminate the output (attenuator 127, port switch C1=C2=0). The source
    // and amp are deliberately left running: power-cycling them per sweep
    // makes the MAX2871's temperature (and so the VCO/PLL state) differ from
    // sweep to sweep.
    rf_state_set_source(RF_STATE_SOURCE_PARKED);
    sweep_allow_unlocked = false;
    sweep_active = false; // the mode stays SWEEP until rf_app_mode_exit()
}

// One detector reading on a coupler path: select it, let the detector's
// output filter settle, take the median of one batch of samples.
static uint16_t measure_path(rf_ctrl_coupler_path_t path) {
    rf_state_set_coupler_path(path);
    sleep_us(RF_CTRL_DETECTOR_SETTLE_US);
    return rf_state_detector_sample().median;
}

// Common start of a sweep point: LPF, retune, then route the source and the
// first coupler path so the switches settle while the PLL locks. Returns
// false if the point cannot be measured (no session, tune failure, cancelled,
// or unlocked without sweep_allow_unlocked); *locked tells whether lock was seen.
static bool start_point(uint64_t freq_hz, uint8_t port, rf_ctrl_coupler_path_t first_path, bool *locked) {
    *locked = false;
    if (!sweep_active || !valid_freq(freq_hz)) return false;
    rf_state_set_lpf_for_freq(freq_hz);
    if (!rf_state_set_freq(freq_hz)) return false;
    // Port first: while parked it is only remembered, then ON routes to it.
    rf_state_set_port(port);
    if (!rf_state_set_source(RF_STATE_SOURCE_ON)) return false; // parked and tuned by rf_app_sweep_begin()
    rf_state_set_coupler_path(first_path);
    sleep_us(RF_CTRL_DETECTOR_SETTLE_US);
    *locked = wait_for_lock(SOURCE_RETUNE_LOCK_TIMEOUT_US, true);
    if (cancelled() || (!*locked && !sweep_allow_unlocked)) return false;
    // The route settle above ran concurrently with the PLL and may already be
    // over by the time LD is stable. The detector (5 kHz output filter) still
    // has to follow the new frequency's level, so wait again after lock.
    sleep_us(RF_APP_FREQ_SETTLE_US);
    return true;
}

rf_app_raw_measurement_t rf_app_sweep_point(uint64_t freq_hz, bool want_s11, bool want_s21,
                                            bool want_s12, bool want_s22) {
    rf_app_raw_measurement_t m = {0};
    // Excitation and detector routing are independent. Sij uses source j.
    // A receiver reading is only reusable within the SAME source state:
    // port1 backward under source1 is reflection; under source2 it is transmission.
    bool need_source1 = want_s11 || want_s21;
    bool need_source2 = want_s12 || want_s22;
    if (!need_source1 && !need_source2) return m;
    bool locked = false;
    if (!start_point(freq_hz, need_source1 ? 1 : 2,
                     need_source1 ? RF_CTRL_COUPLER_PORT1_FWD : RF_CTRL_COUPLER_PORT2_FWD, &locked)) return m;

    if (need_source1) {
        m.source1_ref = rf_state_detector_sample().median;
        if (want_s11) m.source1_reflection = measure_path(RF_CTRL_COUPLER_PORT1_BWD);
        if (want_s21) m.source1_transmission = measure_path(RF_CTRL_COUPLER_PORT2_BWD);
    }
    if (need_source2) {
        if (need_source1) {
            rf_state_set_port(2); // already ON: switches directly
            m.source2_ref = measure_path(RF_CTRL_COUPLER_PORT2_FWD);
        } else {
            m.source2_ref = rf_state_detector_sample().median;
        }
        if (want_s22) m.source2_reflection = measure_path(RF_CTRL_COUPLER_PORT2_BWD);
        if (want_s12) m.source2_transmission = measure_path(RF_CTRL_COUPLER_PORT1_BWD);
    }
    m.valid = locked;
    m.measured = true;
    return m;
}

rf_app_raw_measurement_t rf_app_sweep_paths(uint64_t freq_hz, uint8_t source, uint8_t mask) {
    rf_app_raw_measurement_t m = {0};
    mask &= RF_APP_PATH_MASK_ALL;
    if (!valid_port(source) || mask == 0) return m;
    unsigned first = 0;
    while (!(mask & (1u << first))) ++first;
    bool locked = false;
    if (!start_point(freq_hz, source, (rf_ctrl_coupler_path_t) first, &locked)) return m;

    for (unsigned path = first; path < RF_APP_PATH_COUNT; ++path) {
        if (!(mask & (1u << path))) continue;
        m.path_raw[path] = path == first ? rf_state_detector_sample().median
                                         : measure_path((rf_ctrl_coupler_path_t) path);
    }
    m.valid = locked;
    m.measured = true;
    return m;
}

// --- GENERATOR ---

rf_app_result_t rf_app_generator_start(uint8_t port, uint8_t z, uint8_t atten, uint64_t freq_hz) {
    if (!valid_port(port) || !valid_z(z) || atten > 127 || !valid_freq(freq_hz)) return RF_APP_ERR_BAD_ARGS;
    rf_app_result_t r = require_mode(RF_APP_GENERATOR);
    if (r != RF_APP_OK) return r;
    // Terminate the output before changing impedance, filter or frequency, so
    // the DUT never sees an unlocked or half-configured signal. Re-starting an
    // already running generator therefore interrupts the output briefly.
    generator_on = false;
    rf_state_set_source(RF_STATE_SOURCE_PARKED);
    rf_state_set_z(z);
    rf_state_set_atten(atten); // stored only while parked; reaches the HMC1119 when the source is on below
    rf_state_set_lpf_for_freq(freq_hz);
    if (!rf_state_set_freq(freq_hz)) return RF_APP_ERR_TUNE_FAILED; // includes the power-up double write
    if (!wait_for_lock(SOURCE_INITIAL_LOCK_TIMEOUT_US, true)) return RF_APP_ERR_NOT_LOCKED; // port stays parked
    rf_state_set_port(port);
    // Routes, then applies the requested attenuation. Parked and tuned above,
    // so never refused; if it were, the output stays terminated.
    if (!rf_state_set_source(RF_STATE_SOURCE_ON)) return RF_APP_ERR_NOT_LOCKED;
    generator_on = true;
    return RF_APP_OK;
}

rf_app_result_t rf_app_generator_stop(void) {
    rf_app_result_t r = require_mode(RF_APP_GENERATOR);
    if (r != RF_APP_OK) return r;
    // Same parked state as after a sweep: attenuator 127, port switch terminated.
    // Source and amp keep running (see rf_app_sweep_end()). The mode stays GENERATOR.
    generator_on = false;
    rf_state_set_source(RF_STATE_SOURCE_PARKED);
    return RF_APP_OK;
}

// --- PWR_DETECTOR ---

// The backward-wave coupler path of a port: the same one S11 (port 1) / S22
// (port 2) reads as the reflected wave.
static rf_ctrl_coupler_path_t power_path(uint8_t port) {
    return port == 1 ? RF_CTRL_COUPLER_PORT1_BWD : RF_CTRL_COUPLER_PORT2_BWD;
}

rf_app_result_t rf_app_power_read(uint8_t port, uint8_t z, uint16_t *raw_out) {
    if (!valid_port(port) || !valid_z(z) || raw_out == NULL) return RF_APP_ERR_BAD_ARGS;
    rf_app_result_t r = require_mode(RF_APP_PWR_DETECTOR);
    if (r != RF_APP_OK) return r;
    // A single reading would move the path the continuous measurement sits on.
    if (power_running) return RF_APP_ERR_BUSY;
    // Source terminated so only the signal arriving from the DUT is sampled.
    rf_state_set_source(RF_STATE_SOURCE_PARKED);
    rf_state_set_z(z);
    *raw_out = measure_path(power_path(port));
    return RF_APP_OK;
}

rf_app_result_t rf_app_power_start(uint8_t port, uint8_t z) {
    if (!valid_port(port) || !valid_z(z)) return RF_APP_ERR_BAD_ARGS;
    rf_app_result_t r = require_mode(RF_APP_PWR_DETECTOR);
    if (r != RF_APP_OK) return r;
    // The same input setup as rf_app_power_read(), done once; the samples then
    // only read the detector.
    rf_state_set_source(RF_STATE_SOURCE_PARKED);
    rf_state_set_z(z);
    rf_state_set_coupler_path(power_path(port));
    sleep_us(RF_CTRL_DETECTOR_SETTLE_US);
    power_running = true;
    return RF_APP_OK;
}

rf_app_result_t rf_app_power_sample(uint16_t *raw_out) {
    if (raw_out == NULL) return RF_APP_ERR_BAD_ARGS;
    rf_app_result_t r = require_mode(RF_APP_PWR_DETECTOR);
    if (r != RF_APP_OK) return r;
    if (!power_running) return RF_APP_ERR_WRONG_MODE;
    *raw_out = rf_state_detector_sample().median;
    return RF_APP_OK;
}

rf_app_result_t rf_app_power_stop(void) {
    rf_app_result_t r = require_mode(RF_APP_PWR_DETECTOR);
    if (r != RF_APP_OK) return r;
    power_running = false; // the mode stays PWR_DETECTOR
    return RF_APP_OK;
}

// --- MANUAL ---

// Manual block changes need MANUAL; with needs_terminated, not while the source is on.
static rf_app_result_t check_manual(bool needs_terminated) {
    rf_app_result_t r = require_mode(RF_APP_MANUAL);
    if (r != RF_APP_OK) return r;
    if (needs_terminated && rf_app_source_on()) return RF_APP_ERR_OUTPUT_ACTIVE;
    return RF_APP_OK;
}

rf_app_result_t rf_app_manual_lpf(uint8_t index) {
    if (index >= RF_APP_LPF_COUNT) return RF_APP_ERR_BAD_ARGS;
    rf_app_result_t r = check_manual(true);
    if (r != RF_APP_OK) return r;
    rf_state_set_lpf(index);
    return RF_APP_OK;
}

rf_app_result_t rf_app_manual_coupler(uint8_t path) {
    if (path >= RF_APP_PATH_COUNT) return RF_APP_ERR_BAD_ARGS;
    rf_app_result_t r = check_manual(true);
    if (r != RF_APP_OK) return r;
    rf_state_set_coupler_path((rf_ctrl_coupler_path_t) path);
    return RF_APP_OK;
}

rf_app_result_t rf_app_manual_freq(uint64_t freq_hz) {
    if (!valid_freq(freq_hz)) return RF_APP_ERR_BAD_ARGS;
    rf_app_result_t r = check_manual(true); // retune only with the output terminated
    if (r != RF_APP_OK) return r;
    return rf_state_set_freq(freq_hz) ? RF_APP_OK : RF_APP_ERR_TUNE_FAILED;
}

rf_app_result_t rf_app_manual_port(uint8_t port) {
    if (port > 2) return RF_APP_ERR_BAD_ARGS;
    rf_app_result_t r = check_manual(false);
    if (r != RF_APP_OK) return r;
    if (port == 0) {
        rf_state_set_source(RF_STATE_SOURCE_PARKED);
    } else {
        // Source on only while it runs; otherwise the port is just remembered.
        rf_state_set_port(port);
        if (rf_state_get_source() == RF_STATE_SOURCE_PARKED) rf_state_set_source(RF_STATE_SOURCE_ON); // never from OFF
    }
    return RF_APP_OK;
}

rf_app_result_t rf_app_manual_source(bool on) {
    rf_app_result_t r = check_manual(false);
    if (r != RF_APP_OK) return r;
    // Switching on parks it; an already running source is left as it is.
    if (!on) rf_state_set_source(RF_STATE_SOURCE_OFF);
    else if (rf_state_get_source() == RF_STATE_SOURCE_OFF) rf_state_set_source(RF_STATE_SOURCE_PARKED);
    return RF_APP_OK;
}

// --- Diagnostics and housekeeping ---

rf_app_result_t rf_app_probe_path(rf_app_path_t path, rf_app_detector_sample_t *out) {
    if ((unsigned) path >= (unsigned) RF_APP_PATH_COUNT || out == NULL) return RF_APP_ERR_BAD_ARGS;
    if (mode != RF_APP_GENERATOR && mode != RF_APP_MANUAL) return RF_APP_ERR_WRONG_MODE;
    rf_state_set_coupler_path((rf_ctrl_coupler_path_t) path);
    sleep_us(RF_CTRL_DETECTOR_SETTLE_US);
    rf_ctrl_detector_sample_t sample = rf_state_detector_sample(); // min/max too, so not measure_path()
    *out = (rf_app_detector_sample_t) { .median = sample.median, .min = sample.min, .max = sample.max };
    return RF_APP_OK;
}

rf_app_result_t rf_app_read_temp_c(double *temp_c_out) {
    if (temp_c_out == NULL) return RF_APP_ERR_BAD_ARGS;
    if (sweep_active) return RF_APP_ERR_BUSY;
    if (rf_app_source_on()) return RF_APP_ERR_OUTPUT_ACTIVE;
    return rf_state_read_temp_c(temp_c_out) ? RF_APP_OK : RF_APP_ERR_NOT_VALID;
}

// --- Conversions ---

double rf_app_counts_ratio_db(uint16_t numerator, uint16_t reference) {
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
    const double counts_per_db = AD8318_SLOPE_V_PER_DB * ADC_FULL_SCALE_COUNTS / ADC_VREF_V;
    return ((double) numerator - (double) reference) / counts_per_db;
}

double rf_app_counts_to_volts(uint16_t raw) {
    return (double) raw / ADC_FULL_SCALE_COUNTS * ADC_VREF_V; // 12-bit ADC
}

double rf_app_counts_to_dbm(uint16_t raw) {
    // AD8318: Vout = slope * (Pin - intercept). Datasheet typicals at 900 MHz:
    // slope -24.5 mV/dB with this board's RTADJ (same value rf_app_counts_ratio_db()
    // uses), intercept ~+20 dBm. Referenced to the detector input -- the
    // coupler's coupling factor, switch loss and the 75 ohm path are NOT
    // included, and the slope is not frequency-corrected. Uncalibrated.
    return AD8318_INTERCEPT_DBM + rf_app_counts_to_volts(raw) / AD8318_SLOPE_V_PER_DB;
}

bool rf_app_raw_from_paths(uint8_t source_port, const uint16_t path_raw[RF_APP_PATH_COUNT],
                           uint8_t path_mask, rf_app_raw_measurement_t *out) {
    if (!valid_port(source_port) || out == NULL) return false;
    // Source N's reference is its own forward path, reflection its own backward
    // path, transmission the other port's backward path (rf_app_sweep_point()).
    rf_app_path_t ref = source_port == 1 ? RF_APP_PATH_PORT1_FWD : RF_APP_PATH_PORT2_FWD;
    rf_app_path_t refl = source_port == 1 ? RF_APP_PATH_PORT1_BWD : RF_APP_PATH_PORT2_BWD;
    rf_app_path_t trans = source_port == 1 ? RF_APP_PATH_PORT2_BWD : RF_APP_PATH_PORT1_BWD;
    uint8_t needed = (uint8_t) ((1u << ref) | (1u << refl) | (1u << trans));
    if ((path_mask & needed) != needed) return false;

    rf_app_raw_measurement_t m = {0};
    if (source_port == 1) {
        m.source1_ref = path_raw[ref];
        m.source1_reflection = path_raw[refl];
        m.source1_transmission = path_raw[trans];
    } else {
        m.source2_ref = path_raw[ref];
        m.source2_reflection = path_raw[refl];
        m.source2_transmission = path_raw[trans];
    }
    m.valid = true;
    m.measured = true;
    *out = m;
    return true;
}
