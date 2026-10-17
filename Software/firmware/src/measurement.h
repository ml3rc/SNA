#ifndef SNA_MEASUREMENT_H
#define SNA_MEASUREMENT_H

#include <stdbool.h>
#include <stdint.h>

#include "rf_control/rf_control.h"

// Turns raw detector readings from core 1 into S-parameters in dB with the
// stored EEPROM calibration subtracted. Core 0; no RF hardware.

// S-parameter bits and indices, the order of the wire protocol's `params`, the
// SDI test's s_param and the calibration sets: s11, s21, s12, s22.
#define MEASUREMENT_SPARAMS 4
#define MEASUREMENT_IDX_S11 0
#define MEASUREMENT_IDX_S21 1
#define MEASUREMENT_IDX_S12 2
#define MEASUREMENT_IDX_S22 3
#define MEASUREMENT_S11 (1u << MEASUREMENT_IDX_S11)
#define MEASUREMENT_S21 (1u << MEASUREMENT_IDX_S21)
#define MEASUREMENT_S12 (1u << MEASUREMENT_IDX_S12)
#define MEASUREMENT_S22 (1u << MEASUREMENT_IDX_S22)

// Wire names, by index: "s11", "s21", "s12", "s22".
extern const char *const MEASUREMENT_SPARAM_NAMES[MEASUREMENT_SPARAMS];

typedef struct {
    bool locked;      // stable PLL lock was observed for this point
    bool calibrated;  // every requested parameter had calibration data covering the frequency
    double db[MEASUREMENT_SPARAMS]; // s11, s21, s12, s22; only the requested ones are set
} measurement_t;

// `sparams`: MEASUREMENT_S* bits. Uses the calibration cache, so call
// eeprom_cal_prefetch() first (once per sweep). An unmeasured point gives
// all zero (not locked, not calibrated).
measurement_t measurement_process(const rf_app_raw_measurement_t *raw, uint64_t freq_hz, uint8_t z,
                                  uint8_t sparams);

#endif
