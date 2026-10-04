#ifndef SNA_RF_CONTROL_H
#define SNA_RF_CONTROL_H

#include <stdbool.h>
#include <stdint.h>

#define RF_DETECTOR_SAMPLES 16u
#define RF_DETECTOR_SETTLE_US 500u

typedef struct {
    uint64_t tune_us, path_us, settle_us, lock_wait_us, lock_acquire_us;
    uint64_t adc_us, processing_us, total_us;
    uint32_t adc_reads;
} rf_point_timing_t;

void rf_set_lock_timeout_us(uint32_t timeout_us);
void rf_set_allow_unlocked(bool allow);
rf_point_timing_t rf_get_point_timing(void);

typedef struct {
    bool valid; // true only when stable lock was observed
    bool measured; // numeric fields exist, even if lock was not observed
    double s11_db;
    double s21_db;
    double s12_db;
    double s22_db;
    bool calibrated; // true once eeprom_interpolate() correction was actually applied
} rf_measurement_t;

typedef struct {
    bool valid;
    bool measured;
    uint16_t source1_ref, source1_reflection, source1_transmission;
    uint16_t source2_ref, source2_reflection, source2_transmission;
} rf_raw_measurement_t;

rf_raw_measurement_t rf_acquire_point(uint64_t f, uint8_t z,
                                    bool s11, bool s21, bool s12, bool s22);
rf_measurement_t rf_process_point(rf_raw_measurement_t raw, uint64_t f, uint8_t z,
                                 bool s11, bool s21, bool s12, bool s22);

void rf_control_init(void);

// Bring-up self-test for the detector enable+ADC path: toggles RF_DET_EN and
// checks the ADC reading actually moves. With no RF input present during
// bench bring-up this isn't a calibrated power reading -- it only proves
// RF_DET_EN gates the AD8318 and the ADC/mux read path is alive end to end.
// See rf_control.c for the threshold reasoning.
bool rf_detector_self_test(void);

void rf_set_atten(uint8_t value7bit); // 0-127
uint8_t rf_get_atten(void);
void rf_set_lpf(uint8_t index);       // 0=none, 1=3500MHz, 2=1747MHz, 3=947MHz -- confirmed by the user directly
void rf_set_coupler_raw(uint16_t bits); // raw 12-bit passthrough; see rf_control.c for why this still exists

// Which of the 4 coupler-bank configurations is currently selected --
// confirmed by the user directly, 2026-10-01 (see rf_select_coupler_path()
// in rf_control.c for the full switch matrix). "Forward"/"backward" refer
// to which traveling-wave direction through FL501 ends up sampled onto
// COUPLED (-> AD8318) for that port.
typedef enum {
    RF_COUPLER_PORT1_FWD,
    RF_COUPLER_PORT1_BWD,
    RF_COUPLER_PORT2_FWD,
    RF_COUPLER_PORT2_BWD,
} rf_coupler_path_t;

// Drives the 6-switch coupler bank (U501-U506) into the given configuration.
// Independent of port-select/impedance/filter -- see rf_set_port()/
// rf_set_z()/rf_set_lpf() above, each its own separate stage the caller
// still sets separately.
void rf_select_coupler_path(rf_coupler_path_t path);
void rf_set_port(uint8_t port);       // 1 or 2
uint8_t rf_get_port(void);
void rf_set_z(uint8_t z);             // 50 or 75
uint8_t rf_get_z(void);

void rf_detector_enable(bool en);
uint16_t rf_detector_read_raw(void); // 12-bit ADC counts

void rf_amp_enable(bool en); // RF-Attenuation/Amp stage amplifier power-down control (GPIO_RF_AMP_PWDN)

bool rf_max2871_set_freq(uint64_t freq_hz); // see hal/max2871.c for the register math
void rf_max2871_enable(bool en);
bool rf_max2871_locked(void);
uint8_t rf_max2871_read_die_id(void); // 0111 = MAX2871, 0110 = MAX2870 -- see hal/max2871.c
bool rf_max2871_read_temp_c(double *temp_c_out); // on-chip temp sensor -- see hal/max2871.c

// Call ONCE before a multi-point sweep/measurement session (enables amp/
// detector/source for the given z, including the datasheet's required
// double-write-with-20ms-gap dance for a true power-up) and
// rf_measure_session_end() ONCE after the whole session is done --
// confirmed necessary on real hardware, 2026-10-01, to hit a sweep's
// throughput target (see rf_control.c for the full reasoning). Every
// rf_measure_point() call in between just retunes in place; it does NOT
// itself enable/disable anything.
// Returns false on invalid input or initial lock timeout, with RF disabled.
bool rf_measure_session_start(uint8_t z, uint64_t first_freq_hz);
void rf_measure_session_end(void);

// Core measurement step shared by `sweep` and the SDI self-test. Must be
// called between rf_measure_session_start()/rf_measure_session_end() --
// see those for why. Sets lpf and retunes freq in place, routes the
// coupler bank per the requested S-parameters, converts the AD8318 reading
// to dB (using an approximate, not frequency-corrected slope -- see
// rf_control.c), then subtracts the matching EEPROM SLOT calibration curve
// if one's stored for this z/frequency. `calibrated` is true only if every
// requested parameter had stored calibration data covering freq_hz -- see
// rf_control.c for the exact derivation and remaining caveats (the slope
// approximation itself, and that scalar instruments fundamentally can't
// correct directivity/source-match error without phase, calibrated or not).
rf_measurement_t rf_measure_point(uint64_t freq_hz, uint8_t z,
                                   bool want_s11, bool want_s21,
                                   bool want_s12, bool want_s22);

#endif
