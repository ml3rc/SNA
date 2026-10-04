#ifndef SNA_EEPROM_H
#define SNA_EEPROM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Storage layout implemented here matches Software/protocol/EEPROM_LAYOUT.md
// exactly -- update that doc first if this changes.

#define EEPROM_CAL_POINTS_MAX 240
#define EEPROM_SDI_POINTS_MAX 15

typedef struct {
    uint32_t freq_hz;
    int16_t thresh_db_x100;
    bool cmp_gt; // true = "gt", false = "lt"
    uint8_t s_param; // 0=s11, 1=s21, 2=s12, 3=s22 -- which S-parameter this threshold applies to
} sdi_test_point_t;

void eeprom_init(void);

// Calibration sets, idx 0..7 in the fixed order from EEPROM_LAYOUT.md:
// s11_50, s21_50, s12_50, s22_50, s11_75, s21_75, s12_75, s22_75.
//
// eeprom_cal_set resamples the caller's `count`-point curve onto the fixed
// EEPROM_CAL_POINTS_MAX grid via linear interpolation. All 8 sets share one
// frequency grid; the first cal_set after eeprom_cal_clear_all() establishes
// f0/f1, later calls with a different f0/f1 fail (grid mismatch) -- this
// mirrors PROTOCOL.md's cal_set behavior, which just calls straight through
// to these functions.
// f0/f1 are uint64_t, not uint32_t -- the device sweeps up to 6,000,000,000 Hz,
// which exceeds UINT32_MAX (~4.295 GHz). Confirmed as a real bug on real
// hardware, 2026-10-03: cmd_cal_set used to cast the incoming JSON f1
// straight to (uint32_t), which for f1=6e9 is an out-of-range double-to-
// unsigned-int conversion -- undefined behavior in C, and in practice
// SATURATES to 0xFFFFFFFF on this target's float-to-int conversion, silently
// storing a corrupt grid upper bound. eeprom_interpolate() then computed a
// wildly wrong frequency-to-table-index mapping against that corrupt span
// for every subsequent measurement using that calibration set, producing
// physically-impossible corrected S-parameters (observed: raw values up to
// +18 dB on a passive DUT) -- "data sometimes makes sense, sometimes not"
// depending on which (z, s-param) combination happened to have been
// calibrated near/above ~4.3 GHz. See EEPROM_LAYOUT.md's format_version 2.
bool eeprom_cal_set(int idx, uint64_t f0, uint64_t f1, const double *db_values, size_t count);
bool eeprom_cal_get(int idx, uint64_t *f0, uint64_t *f1, double *db_values, size_t *count);
bool eeprom_cal_clear(int idx);
bool eeprom_cal_clear_all(void);

// Linearly interpolates the stored EEPROM_CAL_POINTS_MAX-point grid for set
// `idx` at `freq_hz`. Returns false if that set isn't calibrated or freq_hz
// falls outside the calibrated range. freq_hz is uint64_t for the same
// reason as eeprom_cal_set's f0/f1 above -- a uint32_t parameter here used
// to silently wrap (not saturate -- this one's an integer narrowing, not a
// float conversion) real frequencies above ~4.3 GHz down to an aliased,
// wrong lookup frequency.
bool eeprom_interpolate(int idx, uint64_t freq_hz, double *out_db);
// Measurement processing must never initiate I2C while the other core samples.
bool eeprom_interpolate_cached(int idx, uint64_t freq_hz, double *out_db);
// Core 0 only. Load calibration once, then interpolate without EEPROM I/O.
void eeprom_cal_prefetch(void);

bool eeprom_sdi_test_set(const sdi_test_point_t *points, size_t count);
void eeprom_sdi_test_get(sdi_test_point_t *points, size_t *count);

// Bring-up validation: writes a known pattern to unused scratch space near
// the end of the EEPROM (the 72 spare bytes after the 8 cal tables, per
// EEPROM_LAYOUT.md -- doesn't touch the header, cal grid, SDI config, or
// any calibration table) and reads it back. Returns true iff the round
// trip matches, i.e. the I2C link and the 24AA32A actually work. Added
// 2026-09-30 specifically so this is checkable with no USB host connected
// (SWD-flash only, see main.c) -- not part of the wire protocol.
bool eeprom_self_test(void);

#endif
