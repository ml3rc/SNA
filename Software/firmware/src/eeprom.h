#ifndef SNA_EEPROM_H
#define SNA_EEPROM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Storage layout implemented here matches Software/protocol/EEPROM_LAYOUT.md
// exactly -- update that doc first if this changes.

#define EEPROM_CAL_POINTS_MAX 240
#define EEPROM_SDI_POINTS_MAX 15
// The S-parameters s11, s21, s12, s22 (in this order): one calibration set
// each per impedance, and the SDI points' s_param encoding.
#define EEPROM_SPARAMS 4
#define EEPROM_CAL_SETS 8 // s11_50, s21_50, s12_50, s22_50, s11_75, s21_75, s12_75, s22_75
// The calibration set of S-parameter `sparam` (0..EEPROM_SPARAMS-1) at impedance `z`.
static inline int eeprom_cal_set_index(uint8_t z, int sparam) {
    return (z == 75 ? EEPROM_SPARAMS : 0) + sparam;
}
// The dB range the 0.01 dB int16 storage can hold (calibration values, SDI thresholds).
#define EEPROM_DB_MIN (INT16_MIN / 100.0)
#define EEPROM_DB_MAX (INT16_MAX / 100.0)

typedef struct {
    uint64_t freq_hz;
    int16_t thresh_db_x100;
    bool cmp_gt; // true = "gt", false = "lt"
    uint8_t s_param; // 0..EEPROM_SPARAMS-1: s11, s21, s12, s22 -- which S-parameter this threshold applies to
} sdi_test_point_t;

// Checks the header and formats a blank or old-layout EEPROM. False if the
// chip could not be read or written (I2C).
bool eeprom_init(void);
// The board revision from the header (EEPROM_LAYOUT.md) as eeprom_init() read it, 0 if it failed.
uint8_t eeprom_hw_revision(void);

// Calibration sets, idx 0..7 in the fixed order from EEPROM_LAYOUT.md:
// s11_50, s21_50, s12_50, s22_50, s11_75, s21_75, s12_75, s22_75.
//
// eeprom_cal_set resamples the caller's `count`-point curve onto the fixed
// EEPROM_CAL_POINTS_MAX grid via linear interpolation. All 8 sets share one
// frequency grid; the first cal_set after eeprom_cal_clear_all() establishes
// f0/f1, later calls with a different f0/f1 fail (grid mismatch) -- this
// mirrors PROTOCOL.md's cal_set behavior, which just calls straight through
// to these functions. Frequencies are uint64_t: the device sweeps up to
// 6 GHz, above UINT32_MAX (a uint32_t grid once corrupted calibrations above
// ~4.3 GHz on real hardware).
typedef enum {
    EEPROM_CAL_OK,
    EEPROM_CAL_BAD_ARGS,      // idx, range or values invalid
    EEPROM_CAL_GRID_MISMATCH, // f0/f1 differ from the stored grid
    EEPROM_CAL_STORAGE_ERROR, // I2C failure; the set is left invalid
} eeprom_cal_result_t;
eeprom_cal_result_t eeprom_cal_set(int idx, uint64_t f0, uint64_t f1, const double *db_values, size_t count);
// Reads set `idx` back: always EEPROM_CAL_POINTS_MAX values (*count says so),
// so db_values must hold that many. False if the set is not calibrated or
// the EEPROM cannot be read.
bool eeprom_cal_get(int idx, uint64_t *f0, uint64_t *f1, double db_values[EEPROM_CAL_POINTS_MAX], size_t *count);
bool eeprom_cal_clear(int idx);
bool eeprom_cal_clear_all(void);

// Core 0 only. Loads the calibration grid and tables once (no-op while the
// cache is valid; every cal write invalidates it).
void eeprom_cal_prefetch(void);
// Linearly interpolates set `idx` at `freq_hz` from the prefetched cache,
// without any EEPROM I/O (processing sweep points never initiates I2C; the
// EEPROM commands are refused while a sweep runs). Returns false if nothing
// was prefetched, the set isn't calibrated or freq_hz falls outside the
// calibrated range.
bool eeprom_interpolate_cached(int idx, uint64_t freq_hz, double *out_db);

// Stores the `count` points (at most EEPROM_SDI_POINTS_MAX are kept).
bool eeprom_sdi_test_set(const sdi_test_point_t *points, size_t count);
// Up to EEPROM_SDI_POINTS_MAX points, so `points` must hold that many. *count
// is 0 if the EEPROM cannot be read or any stored point is invalid.
void eeprom_sdi_test_get(sdi_test_point_t points[EEPROM_SDI_POINTS_MAX], size_t *count);

#endif
