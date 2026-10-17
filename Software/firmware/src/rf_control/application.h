#ifndef SNA_RF_CONTROL_APPLICATION_H
#define SNA_RF_CONTROL_APPLICATION_H

#include <stdbool.h>
#include <stdint.h>

// Application layer: the RF API for everything outside src/rf_control/.
// Calls go application -> state -> block (rf_ctrl_*) -> hal, never past a
// layer. This header exposes none of the layers below, and the layer calls
// nothing above it except through rf_app_hooks_t.
//
// Threading: every function that touches the hardware runs on core 1, the
// only owner of the RF hardware (rf_task.c). The conversions at the end are
// pure and may run on any core.
//
// One application owns the RF hardware at a time (rf_app_mode_t). The host
// enters a mode, uses it and leaves it again; every mode blocks all others:
//
//   IDLE          output parked, source running; the only mode another can be entered from
//   SWEEP         any number of sweep sessions, rf_app_sweep_begin() .. rf_app_sweep_end()
//   GENERATOR     rf_app_generator_start() / stop() (CW output), rf_app_probe_path()
//   PWR_DETECTOR  rf_app_power_read() single readings, rf_app_power_start() / sample() / stop() continuous
//   MANUAL        rf_app_manual_*() bench control, rf_app_probe_path()
//
// rf_app_mode_enter() works only from IDLE (RF_APP_ERR_MODE_ACTIVE otherwise).
// rf_app_mode_exit() returns to IDLE from any mode: it stops the generator and
// the continuous power measurement and parks the source (powering it up again
// if MANUAL switched it off); a running sweep session is ended by the caller
// first. An operation called outside its mode returns RF_APP_ERR_WRONG_MODE.
// rf_app_set_atten() and rf_app_set_z() are settings, usable at any time, also
// between the points of a sweep: they cannot harm the hardware, the host
// decides whether a change breaks its measurement. Only hardware-endangering
// requests are refused.

#define RF_APP_FREQ_SETTLE_US 200u // after PLL lock (stable LD) on a new frequency, before the first sample

// The four coupler-bank paths the detector can read. Also the bit order of
// every path mask (bit 0 = RF_APP_PATH_PORT1_FWD).
typedef enum {
    RF_APP_PATH_PORT1_FWD,
    RF_APP_PATH_PORT1_BWD,
    RF_APP_PATH_PORT2_FWD,
    RF_APP_PATH_PORT2_BWD,
    RF_APP_PATH_COUNT,
} rf_app_path_t;
#define RF_APP_PATH_MASK_ALL ((1u << RF_APP_PATH_COUNT) - 1u)

// LPF bank positions 0..RF_APP_LPF_COUNT-1: none, 3500 MHz, 1747 MHz, 947 MHz.
#define RF_APP_LPF_COUNT 4u

// One detector reading: median of a batch of ADC codes, min/max as its spread
// (noise, instability).
typedef struct { uint16_t median, min, max; } rf_app_detector_sample_t;

// What the layer needs from the code above it, given once to rf_app_init().
// Required: without it rf_app_init() fails (RF_APP_INIT_ERR_NO_HOOKS).
typedef struct {
    // Polled about every millisecond while a sweep or a generator start waits
    // for PLL lock. True gives up (Abort, mode exit, host gone).
    bool (*cancelled)(void);
} rf_app_hooks_t;

typedef enum {
    RF_APP_IDLE,
    RF_APP_GENERATOR,
    RF_APP_SWEEP,
    RF_APP_PWR_DETECTOR,
    RF_APP_MANUAL,
} rf_app_mode_t;

typedef enum {
    RF_APP_OK,
    RF_APP_ERR_BAD_ARGS,
    RF_APP_ERR_BUSY,             // a measurement is running at the time
    RF_APP_ERR_MODE_ACTIVE,      // rf_app_mode_enter() while not IDLE
    RF_APP_ERR_WRONG_MODE,       // operation of another mode
    RF_APP_ERR_OUTPUT_ACTIVE,    // manual lpf/coupler/freq (or a temperature read) while the source is on
    RF_APP_ERR_TUNE_FAILED,
    RF_APP_ERR_NOT_LOCKED,
    RF_APP_ERR_NOT_VALID,        // the temperature sensor reported no valid conversion
} rf_app_result_t;

// Boot self-test of rf_app_init(), as bits (0: all good).
typedef enum {
    RF_APP_INIT_ERR_NOT_LOCKED = 1u << 0, // no stable PLL lock at SNA_FREQ_DEFAULT
    RF_APP_INIT_ERR_DETECTOR = 1u << 1,   // parked detector reading outside RF_APP_DETECTOR_PARKED_MIN/MAX
    RF_APP_INIT_ERR_NO_HOOKS = 1u << 2,   // hooks or hooks->cancelled missing: hardware parked, nothing can run
} rf_app_init_error_t;

// Window for the parked detector (output terminated, attenuator 127): the
// board reads a constant ~2621 counts / 2.11 V there (docs/generator-and-
// power-meter.md). Far outside it the detector is off, unpowered or the ADC
// input is broken.
#define RF_APP_DETECTOR_PARKED_MIN 2200u // 1.77 V
#define RF_APP_DETECTOR_PARKED_MAX 3000u // 2.42 V

// Initializes the RF hardware in RF_APP_IDLE and checks it: source locked at
// SNA_FREQ_DEFAULT, detector reading plausible. Returns rf_app_init_error_t
// bits; the hardware is parked either way. `hooks` is copied. Without hooks
// the self-test is skipped and every lock wait gives up at once, so no sweep
// or generator can run that nothing could cancel.
uint8_t rf_app_init(const rf_app_hooks_t *hooks);
rf_app_mode_t rf_app_get_mode(void);

// SWEEP, GENERATOR, PWR_DETECTOR or MANUAL, only from IDLE.
rf_app_result_t rf_app_mode_enter(rf_app_mode_t mode);
// Back to IDLE from any mode (generator and continuous power measurement
// stopped, source parked and running). RF_APP_ERR_BUSY inside a sweep session.
rf_app_result_t rf_app_mode_exit(void);

// --- Settings and status (any mode) ---
uint8_t rf_app_get_z(void);
uint8_t rf_app_get_port(void);
uint8_t rf_app_get_atten(void);
bool rf_app_source_on(void);
bool rf_app_sweep_active(void);     // inside rf_app_sweep_begin() .. end()
bool rf_app_generator_on(void);     // the CW output is on (start .. stop / mode exit)
uint64_t rf_app_generator_freq(void); // meaningful while the generator is on
bool rf_app_power_running(void);    // continuous power measurement (start .. stop / mode exit)
uint8_t rf_app_power_port(void);    // its port, meaningful while it runs
// 50 or 75. Any time, switched immediately (hot switching is allowed), also
// under a running generator or between sweep points.
rf_app_result_t rf_app_set_z(uint8_t z);
// 0-127 (0.25 dB steps, relative to maximum output). Any time: reaches the
// hardware at once while the source is on (generator, manual output, a sweep
// between two points), otherwise when it is switched on.
rf_app_result_t rf_app_set_atten(uint8_t atten);

// --- SWEEP ---
typedef struct {
    bool valid; // true only when stable lock was observed
    bool measured; // the readings exist, even if lock was not observed
    uint16_t source1_ref, source1_reflection, source1_transmission;
    uint16_t source2_ref, source2_reflection, source2_transmission;
    uint16_t path_raw[RF_APP_PATH_COUNT]; // raw detector sweep: one code per coupler path, only the requested ones are set
} rf_app_raw_measurement_t;

// Call ONCE before a multi-point sweep and rf_app_sweep_end() ONCE after it
// (always, also when begin failed). Needs SWEEP and no running session; parks
// the output, sets z and the attenuation `atten` (every sweep brings its own
// level, it stays the setting afterwards), tunes f0 (with the power-up double
// write if the source was off) and waits up to 2 s for lock. allow_unlocked:
// points whose PLL does not lock in time are still measured and only flagged
// (valid=false). Returns false on invalid input, wrong mode, a running session,
// cancellation or (strict) initial lock timeout.
bool rf_app_sweep_begin(uint8_t z, uint64_t first_freq_hz, bool allow_unlocked, uint8_t atten);
// Parks the output (source and amp keep running so their temperature stays
// stable). The mode stays SWEEP. No-op without a session.
void rf_app_sweep_end(void);

// One sweep point: sets the LPF, retunes in place, routes the source and
// coupler bank per the requested S-parameters and reads the detector. Only
// within rf_app_sweep_begin()/end().
rf_app_raw_measurement_t rf_app_sweep_point(uint64_t f, bool s11, bool s21, bool s12, bool s22);
// Raw detector point: the same acquisition (LPF, retune, lock wait, settling) but instead of
// S-parameter readings it samples the requested coupler paths with the source on at `source_port`.
// path_mask: rf_app_path_t bits.
rf_app_raw_measurement_t rf_app_sweep_paths(uint64_t f, uint8_t source_port, uint8_t path_mask);

// --- GENERATOR ---
// Continuous wave at one port: parks the output, sets impedance, attenuation,
// LPF and frequency, waits for PLL lock, then routes the source to `port`.
// On failure (also when cancelled while waiting) the output stays parked and
// the generator off. Restarting a running generator interrupts the output
// briefly. Both need GENERATOR.
rf_app_result_t rf_app_generator_start(uint8_t port, uint8_t z, uint8_t atten, uint64_t freq_hz);
rf_app_result_t rf_app_generator_stop(void); // parks the output, the mode stays GENERATOR

// --- PWR_DETECTOR ---
// Turns the source off (parks it) and samples the detector on the backward-wave
// coupler path of `port` (power arriving from the external device). Needs
// PWR_DETECTOR; RF_APP_ERR_BUSY during a continuous measurement.
rf_app_result_t rf_app_power_read(uint8_t port, uint8_t z, uint16_t *raw_out);
// Continuous measurement: sets the input up once (output terminated, z, the
// backward path of `port`, detector settled). Calling it again while running
// switches to the new settings. Needs PWR_DETECTOR.
rf_app_result_t rf_app_power_start(uint8_t port, uint8_t z);
// One reading of the running continuous measurement, on the path set up by
// rf_app_power_start() (no re-select, no settle wait). RF_APP_ERR_WRONG_MODE
// when it is not running.
rf_app_result_t rf_app_power_sample(uint16_t *raw_out);
// Ends the continuous measurement; the mode stays PWR_DETECTOR. Also fine when not running.
rf_app_result_t rf_app_power_stop(void);

// --- MANUAL (bench control of single blocks), all need MANUAL ---
// lpf, coupler and freq need the output terminated (RF_APP_ERR_OUTPUT_ACTIVE otherwise).
rf_app_result_t rf_app_manual_lpf(uint8_t index);
rf_app_result_t rf_app_manual_coupler(uint8_t path);
rf_app_result_t rf_app_manual_freq(uint64_t freq_hz);
// 0 terminates the output; 1/2 select the source port and switch the source on
// if it is running. rf_app_source_on() tells whether it is on now.
rf_app_result_t rf_app_manual_port(uint8_t port);
// The RF source as a whole (MAX2871 + amp + attenuator): on parks it powered
// up, off terminates and powers it down.
rf_app_result_t rf_app_manual_source(bool on);

// --- Diagnostics and housekeeping ---
// Reads one coupler path with the output left exactly as it is: selects the
// path, waits the detector settle time and takes one batch of samples.
// Needs GENERATOR or MANUAL.
rf_app_result_t rf_app_probe_path(rf_app_path_t path, rf_app_detector_sample_t *out);
// MAX2871 temperature. Refused during a sweep session (BUSY) or with the
// source on (OUTPUT_ACTIVE): the read rewrites PLL registers.
rf_app_result_t rf_app_read_temp_c(double *temp_c_out);

// --- Conversions (pure, any core) ---
// The dB difference of two detector readings (numerator minus reference).
double rf_app_counts_ratio_db(uint16_t numerator, uint16_t reference);
// Detector output voltage for a raw ADC code.
double rf_app_counts_to_volts(uint16_t raw);
// Approximate detector-input dBm for a raw ADC code -- see application.c.
double rf_app_counts_to_dbm(uint16_t raw);
// The S-parameter fields of source `source_port` (reference, reflection,
// transmission) from single path readings, with the same path roles as
// rf_app_sweep_point(). path_mask: which entries of path_raw were read.
// False if the port is invalid or one of its three paths was not read.
bool rf_app_raw_from_paths(uint8_t source_port, const uint16_t path_raw[RF_APP_PATH_COUNT],
                           uint8_t path_mask, rf_app_raw_measurement_t *out);

#endif
