// Structured calibration/config storage layer: implements the byte layout
// in Software/protocol/EEPROM_LAYOUT.md (header, cal grid, SDI thresholds,
// 8 calibration tables) on top of the raw chip-level read/write primitives
// in hal/eeprom_24aa32a.c. This file knows the *meaning* of the bytes; the
// HAL module below it just knows how to move bytes in and out of a
// 24AA32A.
#include "frequency_limits.h"

#include "eeprom.h"

#include <math.h>
#include <string.h>

#include "hal/eeprom_24aa32a.h"

// Layout offsets -- must match Software/protocol/EEPROM_LAYOUT.md exactly.
// OFF_SDI_CONFIG/OFF_CAL_TABLES shifted +8 vs format_version 1 (eeprom_cal_grid_t
// grew from 16 to 24 bytes when cal_f0_hz/cal_f1_hz widened to uint64_t -- see
// the format_version bump below and the comment on eeprom_cal_grid_t).
#define OFF_HEADER            0x000
#define OFF_CAL_GRID          0x020
#define OFF_SDI_CONFIG         0x038
#define OFF_CAL_TABLES         0x0B8
#define CAL_TABLE_SIZE_BYTES   (EEPROM_CAL_POINTS_MAX * 2) // 480

// 8 cal tables end at 0xB8 + 3840 = 0xFB8 (4024); EEPROM is 4096 bytes, so
// bytes 4024-4095 (72 bytes) are unused spare space -- safe scratch area
// for eeprom_self_test() below, well clear of the edge on both sides.
#define OFF_SELF_TEST 4080

#define HEADER_MAGIC "SNA1"
// Bumped 1 -> 2, 2026-10-03: eeprom_cal_grid_t's cal_f0_hz/cal_f1_hz widened
// from uint32_t to uint64_t (a uint32_t can't represent this device's up-to-
// 6,000,000,000 Hz range -- see eeprom_cal_grid_t's comment for the exact
// corruption this caused on real hardware), which shifted every offset after
// OFF_CAL_GRID. eeprom_init() below treats a stored format_version != this
// as equivalent to blank/uninitialized EEPROM, so any device that was ever
// calibrated under the old layout gets cleanly wiped and reinitialized under
// the new one instead of misinterpreting old-layout bytes as new-layout
// fields.
#define FORMAT_VERSION 2

// Preserve older format-2 grids that still cover the supported measurement band.
#define LEGACY_CAL_MIN_HZ 23500000ULL

#pragma pack(push, 1)
typedef struct {
    char magic[4];
    uint8_t format_version;
    uint8_t hw_revision;
    uint16_t serial;
    uint8_t reserved[24];
} eeprom_header_t; // 32 bytes

typedef struct {
    // uint64_t, not uint32_t -- this device sweeps up to 6,000,000,000 Hz,
    // which exceeds UINT32_MAX (~4.295 GHz). See FORMAT_VERSION above for
    // the full story of the real-hardware bug this fixes.
    uint64_t cal_f0_hz;
    uint64_t cal_f1_hz;
    uint16_t cal_points;
    uint8_t cal_valid_mask; // bit i = calibration set i has data
    uint8_t reserved[5];
} eeprom_cal_grid_t; // 24 bytes

typedef struct {
    uint32_t f_hz;
    int16_t thresh_db_x100;
    uint8_t cmp; // 0 = lt, 1 = gt
    uint8_t s_param; // 0=s11, 1=s21, 2=s12, 3=s22 -- repurposed from a genuinely unused
                      // `reserved` byte, 2026-10-02, so this costs zero EEPROM layout size
                      // change (see EEPROM_LAYOUT.md)
} eeprom_sdi_point_t; // 8 bytes

typedef struct {
    uint8_t point_count;
    uint8_t reserved[7];
    eeprom_sdi_point_t points[EEPROM_SDI_POINTS_MAX];
} eeprom_sdi_config_t; // 128 bytes
#pragma pack(pop)

_Static_assert(sizeof(eeprom_header_t) == 32, "eeprom_header_t size drifted from EEPROM_LAYOUT.md");
_Static_assert(sizeof(eeprom_cal_grid_t) == 24, "eeprom_cal_grid_t size drifted from EEPROM_LAYOUT.md");
_Static_assert(sizeof(eeprom_sdi_config_t) == 128, "eeprom_sdi_config_t size drifted from EEPROM_LAYOUT.md");
_Static_assert(OFF_CAL_TABLES + 8 * CAL_TABLE_SIZE_BYTES <= EEPROM_24AA32A_SIZE_BYTES, "8 cal tables don't fit in the 4KB EEPROM");

static bool cal_grid_read(eeprom_cal_grid_t *grid) {
    return eeprom_24aa32a_read(OFF_CAL_GRID, (uint8_t *) grid, sizeof(*grid));
}

static bool cache_loaded;
static eeprom_cal_grid_t cached_grid;
static int16_t cached_tables[8][EEPROM_CAL_POINTS_MAX];
static void invalidate_cache(void) { cache_loaded = false; }

void eeprom_cal_prefetch(void) {
    if (cache_loaded) return;
    memset(&cached_grid, 0, sizeof(cached_grid));
    if (!cal_grid_read(&cached_grid)) return;
    for (int idx = 0; idx < 8; idx++) {
        if (!(cached_grid.cal_valid_mask & (1u << idx))) continue;
        uint16_t addr = (uint16_t)(OFF_CAL_TABLES + idx * CAL_TABLE_SIZE_BYTES);
        if (!eeprom_24aa32a_read(addr, (uint8_t *)cached_tables[idx], sizeof(cached_tables[idx])))
            cached_grid.cal_valid_mask &= (uint8_t)~(1u << idx);
    }
    cache_loaded = true;
}

static bool cal_grid_write(const eeprom_cal_grid_t *grid) {
    return eeprom_24aa32a_write(OFF_CAL_GRID, (const uint8_t *) grid, sizeof(*grid));
}

void eeprom_init(void) {
    invalidate_cache();
    eeprom_24aa32a_init();

    eeprom_header_t hdr;
    if (!eeprom_24aa32a_read(OFF_HEADER, (uint8_t *) &hdr, sizeof(hdr))) return;

    if (memcmp(hdr.magic, HEADER_MAGIC, 4) != 0 || hdr.format_version != FORMAT_VERSION) {
        // Blank/uninitialized EEPROM (or first boot on fresh hardware), OR a
        // device that was written under an older, incompatible layout
        // (format_version mismatch -- see FORMAT_VERSION's comment above):
        // write a fresh header and zeroed cal grid / SDI config either way,
        // rather than risk misinterpreting old-layout bytes as new-layout
        // fields.
        memset(&hdr, 0, sizeof(hdr));
        memcpy(hdr.magic, HEADER_MAGIC, 4);
        hdr.format_version = FORMAT_VERSION;
        hdr.hw_revision = 1;
        hdr.serial = 0;
        eeprom_cal_grid_t grid = {0};
        if (!cal_grid_write(&grid)) return;

        eeprom_sdi_config_t sdi = {0};
        if (!eeprom_24aa32a_write(OFF_SDI_CONFIG, (uint8_t *) &sdi, sizeof(sdi))) return;
        // Commit the format header last: failed initialization must be retried next boot.
        eeprom_24aa32a_write(OFF_HEADER, (uint8_t *) &hdr, sizeof(hdr));
    }
}

bool eeprom_cal_set(int idx, uint64_t f0, uint64_t f1, const double *db_values, size_t count) {
    invalidate_cache();
    if (idx < 0 || idx >= 8 || count == 0 || !db_values ||
        f0 < SNA_FREQ_MIN_HZ || f1 > SNA_FREQ_MAX_HZ || f0 > f1) return false;
    for (size_t i = 0; i < count; i++) {
        if (!isfinite(db_values[i]) || db_values[i] < -327.68 || db_values[i] > 327.67) return false;
    }

    eeprom_cal_grid_t grid;
    if (!cal_grid_read(&grid)) return false;

    bool grid_established = (grid.cal_valid_mask != 0);
    if (!grid_established) {
        grid.cal_f0_hz = f0;
        grid.cal_f1_hz = f1;
        grid.cal_points = EEPROM_CAL_POINTS_MAX;
    } else if (grid.cal_f0_hz != f0 || grid.cal_f1_hz != f1) {
        return false; // grid mismatch -- protocol.c reports this to the client
    }

    // Resample the caller's `count`-point curve onto the fixed
    // EEPROM_CAL_POINTS_MAX grid (the wire protocol lets a client upload any
    // point count; storage always normalizes to this fixed grid).
    int16_t table[EEPROM_CAL_POINTS_MAX];
    for (int i = 0; i < EEPROM_CAL_POINTS_MAX; i++) {
        double frac = (EEPROM_CAL_POINTS_MAX > 1) ? ((double) i / (double) (EEPROM_CAL_POINTS_MAX - 1)) : 0.0;
        double src_pos = frac * (double) (count - 1);
        size_t lo = (size_t) src_pos;
        size_t hi = (lo + 1 < count) ? lo + 1 : lo;
        double t = src_pos - (double) lo;
        double val = db_values[lo] * (1.0 - t) + db_values[hi] * t;
        table[i] = (int16_t) lround(val * 100.0);
    }

    uint16_t table_addr = (uint16_t) (OFF_CAL_TABLES + idx * CAL_TABLE_SIZE_BYTES);
    // Invalidate before overwriting: a partial I2C write must not leave a
    // damaged table marked calibrated. Other tables keep their validity.
    if (grid.cal_valid_mask & (1 << idx)) {
        grid.cal_valid_mask &= (uint8_t) ~(1 << idx);
        if (!cal_grid_write(&grid)) return false;
    }
    if (!eeprom_24aa32a_write(table_addr, (const uint8_t *) table, sizeof(table))) return false;

    grid.cal_valid_mask |= (uint8_t) (1 << idx);
    return cal_grid_write(&grid);
}

bool eeprom_cal_get(int idx, uint64_t *f0, uint64_t *f1, double *db_values, size_t *count) {
    if (idx < 0 || idx >= 8) return false;

    eeprom_cal_grid_t grid;
    if (!cal_grid_read(&grid)) return false;
    if (!(grid.cal_valid_mask & (1 << idx))) return false;
    if (grid.cal_points != EEPROM_CAL_POINTS_MAX || grid.cal_f0_hz > grid.cal_f1_hz ||
        grid.cal_f0_hz < LEGACY_CAL_MIN_HZ || grid.cal_f1_hz > SNA_FREQ_MAX_HZ) return false;

    int16_t table[EEPROM_CAL_POINTS_MAX];
    uint16_t table_addr = (uint16_t) (OFF_CAL_TABLES + idx * CAL_TABLE_SIZE_BYTES);
    if (!eeprom_24aa32a_read(table_addr, (uint8_t *) table, sizeof(table))) return false;

    *f0 = grid.cal_f0_hz;
    *f1 = grid.cal_f1_hz;
    *count = EEPROM_CAL_POINTS_MAX;
    for (int i = 0; i < EEPROM_CAL_POINTS_MAX; i++) {
        db_values[i] = table[i] / 100.0;
    }
    return true;
}

bool eeprom_cal_clear(int idx) {
    invalidate_cache();
    if (idx < 0 || idx >= 8) return false;
    eeprom_cal_grid_t grid;
    if (!cal_grid_read(&grid)) return false;
    grid.cal_valid_mask &= (uint8_t) ~(1 << idx);
    if (grid.cal_valid_mask == 0) {
        grid.cal_f0_hz = 0;
        grid.cal_f1_hz = 0;
        grid.cal_points = 0;
    }
    return cal_grid_write(&grid);
}

bool eeprom_cal_clear_all(void) {
    invalidate_cache();
    eeprom_cal_grid_t grid = {0};
    return cal_grid_write(&grid);
}

bool eeprom_interpolate(int idx, uint64_t freq_hz, double *out_db) {
    eeprom_cal_prefetch();
    return eeprom_interpolate_cached(idx, freq_hz, out_db);
}

bool eeprom_interpolate_cached(int idx, uint64_t freq_hz, double *out_db) {
    if (freq_hz < SNA_FREQ_MIN_HZ || freq_hz > SNA_FREQ_MAX_HZ) return false;
    if (idx < 0 || idx >= 8) return false;
    if (!cache_loaded) return false;
    eeprom_cal_grid_t grid = cached_grid;
    if (!(grid.cal_valid_mask & (1 << idx))) return false;
    if (grid.cal_points != EEPROM_CAL_POINTS_MAX || grid.cal_f0_hz > grid.cal_f1_hz ||
        grid.cal_f0_hz < LEGACY_CAL_MIN_HZ || grid.cal_f1_hz > SNA_FREQ_MAX_HZ) return false;
    if (freq_hz < grid.cal_f0_hz || freq_hz > grid.cal_f1_hz) return false;

    const int16_t *table = cached_tables[idx];

    double span = (double) (grid.cal_f1_hz - grid.cal_f0_hz);
    double frac = (span > 0) ? ((double) (freq_hz - grid.cal_f0_hz) / span) : 0.0;
    double pos = frac * (EEPROM_CAL_POINTS_MAX - 1);
    size_t lo = (size_t) pos;
    size_t hi = (lo + 1 < EEPROM_CAL_POINTS_MAX) ? lo + 1 : lo;
    double t = pos - (double) lo;

    double lo_db = table[lo] / 100.0;
    double hi_db = table[hi] / 100.0;
    *out_db = lo_db * (1.0 - t) + hi_db * t;
    return true;
}

bool eeprom_sdi_test_set(const sdi_test_point_t *points, size_t count) {
    if (count > EEPROM_SDI_POINTS_MAX) count = EEPROM_SDI_POINTS_MAX;

    eeprom_sdi_config_t cfg = {0};
    cfg.point_count = (uint8_t) count;
    for (size_t i = 0; i < count; i++) {
        cfg.points[i].f_hz = points[i].freq_hz;
        cfg.points[i].thresh_db_x100 = points[i].thresh_db_x100;
        cfg.points[i].cmp = points[i].cmp_gt ? 1 : 0;
        cfg.points[i].s_param = points[i].s_param;
    }
    return eeprom_24aa32a_write(OFF_SDI_CONFIG, (const uint8_t *) &cfg, sizeof(cfg));
}

bool eeprom_self_test(void) {
    static const uint8_t pattern[16] = {
        0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x23, 0x45, 0x67,
        0x89, 0xAB, 0xCD, 0xEF, 0x55, 0xAA, 0x5A, 0xA5,
    };
    uint8_t readback[sizeof(pattern)] = {0};

    if (!eeprom_24aa32a_write(OFF_SELF_TEST, pattern, sizeof(pattern))) return false;
    if (!eeprom_24aa32a_read(OFF_SELF_TEST, readback, sizeof(readback))) return false;
    return memcmp(pattern, readback, sizeof(pattern)) == 0;
}

void eeprom_sdi_test_get(sdi_test_point_t *points, size_t *count) {
    eeprom_sdi_config_t cfg;
    if (!eeprom_24aa32a_read(OFF_SDI_CONFIG, (uint8_t *) &cfg, sizeof(cfg)) ||
        cfg.point_count > EEPROM_SDI_POINTS_MAX) {
        *count = 0;
        return;
    }
    *count = cfg.point_count;
    for (size_t i = 0; i < cfg.point_count; i++) {
        if (cfg.points[i].f_hz < SNA_FREQ_MIN_HZ || cfg.points[i].cmp > 1 ||
            cfg.points[i].s_param > 3) {
            *count = 0;
            return;
        }
        points[i].freq_hz = cfg.points[i].f_hz;
        points[i].thresh_db_x100 = cfg.points[i].thresh_db_x100;
        points[i].cmp_gt = (cfg.points[i].cmp != 0);
        points[i].s_param = cfg.points[i].s_param;
    }
}
