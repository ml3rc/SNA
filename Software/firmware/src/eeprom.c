// Structured calibration/config storage layer: implements the byte layout
// in Software/docs/firmware/protocol/EEPROM_LAYOUT.md (header, cal grid, SDI thresholds,
// 8 calibration tables) on top of the raw chip-level read/write primitives
// in hal/eeprom_24aa32a.c. This file knows the *meaning* of the bytes; the
// HAL module below it just knows how to move bytes in and out of a
// 24AA32A.
#include "frequency_limits.h"

#include "eeprom.h"

#include <math.h>
#include <string.h>

#include "hal/eeprom_24aa32a.h"

// Layout offsets -- must match Software/docs/firmware/protocol/EEPROM_LAYOUT.md exactly.
#define OFF_HEADER            0x000
#define OFF_CAL_GRID          0x020
#define OFF_SDI_CONFIG         0x038
#define OFF_CAL_TABLES         0x0F4
#define CAL_TABLE_SIZE_BYTES   (EEPROM_CAL_POINTS_MAX * 2) // 480

#define HEADER_MAGIC "SNA1"
// 3: SDI point frequencies widened to uint64_t (12G-SDI up to 6 GHz), which
// moved the calibration tables. eeprom_init() treats a stored format_version
// != this like a blank EEPROM and formats it, so old-layout bytes are never
// misread as new-layout fields (calibration has to be redone after an update).
#define FORMAT_VERSION 3
#define HW_REVISION 1 // written when formatting

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
    uint64_t f_hz;
    int16_t thresh_db_x100;
    uint8_t cmp; // 0 = lt, 1 = gt
    uint8_t s_param; // 0=s11, 1=s21, 2=s12, 3=s22
} eeprom_sdi_point_t; // 12 bytes

typedef struct {
    uint8_t point_count;
    uint8_t reserved[7];
    eeprom_sdi_point_t points[EEPROM_SDI_POINTS_MAX];
} eeprom_sdi_config_t; // 188 bytes
#pragma pack(pop)

_Static_assert(sizeof(eeprom_header_t) == 32, "eeprom_header_t size drifted from EEPROM_LAYOUT.md");
_Static_assert(sizeof(eeprom_cal_grid_t) == 24, "eeprom_cal_grid_t size drifted from EEPROM_LAYOUT.md");
_Static_assert(sizeof(eeprom_sdi_config_t) == 188, "eeprom_sdi_config_t size drifted from EEPROM_LAYOUT.md");
_Static_assert(OFF_SDI_CONFIG + sizeof(eeprom_sdi_config_t) == OFF_CAL_TABLES, "SDI config and cal tables overlap or leave a gap");
_Static_assert(OFF_HEADER + sizeof(eeprom_header_t) == OFF_CAL_GRID, "header and cal grid overlap or leave a gap");
_Static_assert(OFF_CAL_GRID + sizeof(eeprom_cal_grid_t) == OFF_SDI_CONFIG, "cal grid and SDI config overlap or leave a gap");
_Static_assert(OFF_CAL_TABLES + EEPROM_CAL_SETS * CAL_TABLE_SIZE_BYTES <= HAL_EEPROM_24AA32A_SIZE_BYTES, "the cal tables don't fit in the 4KB EEPROM");
_Static_assert(EEPROM_CAL_SETS == 2 * EEPROM_SPARAMS, "one set per S-parameter at 50 and at 75 ohm");
_Static_assert(EEPROM_CAL_SETS <= 8 * sizeof(((eeprom_cal_grid_t *) 0)->cal_valid_mask), "cal_valid_mask has a bit per set");
_Static_assert(EEPROM_SDI_POINTS_MAX <= UINT8_MAX, "point_count is a uint8_t");

static bool cal_grid_read(eeprom_cal_grid_t *grid) {
    return hal_eeprom_24aa32a_read(OFF_CAL_GRID, (uint8_t *) grid, sizeof(*grid));
}

static bool cache_loaded;
static eeprom_cal_grid_t cached_grid;
static int16_t cached_tables[EEPROM_CAL_SETS][EEPROM_CAL_POINTS_MAX];
static void invalidate_cache(void) { cache_loaded = false; }

void eeprom_cal_prefetch(void) {
    if (cache_loaded) return;
    memset(&cached_grid, 0, sizeof(cached_grid));
    if (!cal_grid_read(&cached_grid)) return;
    bool complete = true;
    for (int idx = 0; idx < EEPROM_CAL_SETS; idx++) {
        if (!(cached_grid.cal_valid_mask & (1u << idx))) continue;
        uint16_t addr = (uint16_t)(OFF_CAL_TABLES + idx * CAL_TABLE_SIZE_BYTES);
        if (!hal_eeprom_24aa32a_read(addr, (uint8_t *)cached_tables[idx], sizeof(cached_tables[idx]))) {
            cached_grid.cal_valid_mask &= (uint8_t)~(1u << idx);
            complete = false;
        }
    }
    // A set that could not be read is uncalibrated for now; the next prefetch
    // reads it again instead of one I2C glitch costing it until a reboot.
    cache_loaded = complete;
}

static bool cal_grid_write(const eeprom_cal_grid_t *grid) {
    return hal_eeprom_24aa32a_write(OFF_CAL_GRID, (const uint8_t *) grid, sizeof(*grid));
}

static uint8_t hw_revision; // from the header, read by eeprom_init()

uint8_t eeprom_hw_revision(void) { return hw_revision; }

bool eeprom_init(void) {
    invalidate_cache();
    hw_revision = 0;
    hal_eeprom_24aa32a_init();

    eeprom_header_t hdr;
    bool header_ok = false;
    // The header has no CRC: read it twice before concluding that the EEPROM is
    // blank or foreign and wiping it, so a single bad read cannot cost the calibration.
    for (int attempt = 0; attempt < 2 && !header_ok; attempt++) {
        if (!hal_eeprom_24aa32a_read(OFF_HEADER, (uint8_t *) &hdr, sizeof(hdr))) return false;
        header_ok = memcmp(hdr.magic, HEADER_MAGIC, 4) == 0 && hdr.format_version == FORMAT_VERSION;
    }

    if (!header_ok) {
        // Blank/uninitialized EEPROM (or first boot on fresh hardware), OR a
        // device that was written under an older, incompatible layout
        // (format_version mismatch -- see FORMAT_VERSION's comment above):
        // write a fresh header and zeroed cal grid / SDI config either way,
        // rather than risk misinterpreting old-layout bytes as new-layout
        // fields.
        memset(&hdr, 0, sizeof(hdr));
        memcpy(hdr.magic, HEADER_MAGIC, 4);
        hdr.format_version = FORMAT_VERSION;
        hdr.hw_revision = HW_REVISION;
        hdr.serial = 0;
        eeprom_cal_grid_t grid = {0};
        if (!cal_grid_write(&grid)) return false;

        eeprom_sdi_config_t sdi = {0};
        if (!hal_eeprom_24aa32a_write(OFF_SDI_CONFIG, (uint8_t *) &sdi, sizeof(sdi))) return false;
        // Commit the format header last: failed initialization must be retried next boot.
        if (!hal_eeprom_24aa32a_write(OFF_HEADER, (uint8_t *) &hdr, sizeof(hdr))) return false;
    }
    hw_revision = hdr.hw_revision;
    return true;
}

eeprom_cal_result_t eeprom_cal_set(int idx, uint64_t f0, uint64_t f1, const double *db_values, size_t count) {
    invalidate_cache();
    if (idx < 0 || idx >= EEPROM_CAL_SETS || count == 0 || count > EEPROM_CAL_POINTS_MAX || !db_values ||
        f0 < SNA_FREQ_MIN_HZ || f1 > SNA_FREQ_MAX_HZ || f0 > f1) return EEPROM_CAL_BAD_ARGS;
    for (size_t i = 0; i < count; i++) {
        if (!isfinite(db_values[i]) || db_values[i] < EEPROM_DB_MIN || db_values[i] > EEPROM_DB_MAX) return EEPROM_CAL_BAD_ARGS;
    }

    eeprom_cal_grid_t grid;
    if (!cal_grid_read(&grid)) return EEPROM_CAL_STORAGE_ERROR;

    bool grid_established = (grid.cal_valid_mask != 0);
    if (!grid_established) {
        grid.cal_f0_hz = f0;
        grid.cal_f1_hz = f1;
        grid.cal_points = EEPROM_CAL_POINTS_MAX;
    } else if (grid.cal_f0_hz != f0 || grid.cal_f1_hz != f1) {
        return EEPROM_CAL_GRID_MISMATCH;
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
        if (!cal_grid_write(&grid)) return EEPROM_CAL_STORAGE_ERROR;
    }
    if (!hal_eeprom_24aa32a_write(table_addr, (const uint8_t *) table, sizeof(table))) return EEPROM_CAL_STORAGE_ERROR;

    grid.cal_valid_mask |= (uint8_t) (1 << idx);
    return cal_grid_write(&grid) ? EEPROM_CAL_OK : EEPROM_CAL_STORAGE_ERROR;
}

bool eeprom_cal_get(int idx, uint64_t *f0, uint64_t *f1, double db_values[EEPROM_CAL_POINTS_MAX], size_t *count) {
    if (idx < 0 || idx >= EEPROM_CAL_SETS) return false;

    eeprom_cal_grid_t grid;
    if (!cal_grid_read(&grid)) return false;
    if (!(grid.cal_valid_mask & (1 << idx))) return false;
    if (grid.cal_points != EEPROM_CAL_POINTS_MAX || grid.cal_f0_hz > grid.cal_f1_hz ||
        grid.cal_f0_hz < SNA_FREQ_MIN_HZ || grid.cal_f1_hz > SNA_FREQ_MAX_HZ) return false;

    int16_t table[EEPROM_CAL_POINTS_MAX];
    uint16_t table_addr = (uint16_t) (OFF_CAL_TABLES + idx * CAL_TABLE_SIZE_BYTES);
    if (!hal_eeprom_24aa32a_read(table_addr, (uint8_t *) table, sizeof(table))) return false;

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
    if (idx < 0 || idx >= EEPROM_CAL_SETS) return false;
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

bool eeprom_interpolate_cached(int idx, uint64_t freq_hz, double *out_db) {
    if (freq_hz < SNA_FREQ_MIN_HZ || freq_hz > SNA_FREQ_MAX_HZ) return false;
    if (idx < 0 || idx >= EEPROM_CAL_SETS) return false;
    if (!cache_loaded) return false;
    eeprom_cal_grid_t grid = cached_grid;
    if (!(grid.cal_valid_mask & (1 << idx))) return false;
    if (grid.cal_points != EEPROM_CAL_POINTS_MAX || grid.cal_f0_hz > grid.cal_f1_hz ||
        grid.cal_f0_hz < SNA_FREQ_MIN_HZ || grid.cal_f1_hz > SNA_FREQ_MAX_HZ) return false;
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
    return hal_eeprom_24aa32a_write(OFF_SDI_CONFIG, (const uint8_t *) &cfg, sizeof(cfg));
}

void eeprom_sdi_test_get(sdi_test_point_t points[EEPROM_SDI_POINTS_MAX], size_t *count) {
    *count = 0; // until every point has been checked
    eeprom_sdi_config_t cfg;
    if (!hal_eeprom_24aa32a_read(OFF_SDI_CONFIG, (uint8_t *) &cfg, sizeof(cfg)) ||
        cfg.point_count > EEPROM_SDI_POINTS_MAX) return;
    for (size_t i = 0; i < cfg.point_count; i++) {
        if (cfg.points[i].f_hz < SNA_FREQ_MIN_HZ || cfg.points[i].f_hz > SNA_FREQ_MAX_HZ || cfg.points[i].cmp > 1 ||
            cfg.points[i].s_param >= EEPROM_SPARAMS) return; // a damaged config reads as empty
        points[i].freq_hz = cfg.points[i].f_hz;
        points[i].thresh_db_x100 = cfg.points[i].thresh_db_x100;
        points[i].cmp_gt = (cfg.points[i].cmp != 0);
        points[i].s_param = cfg.points[i].s_param;
    }
    *count = cfg.point_count;
}
