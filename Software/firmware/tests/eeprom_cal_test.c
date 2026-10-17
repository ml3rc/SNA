// Host-only regression test. The EEPROM HAL is backed by RAM; no hardware is accessed.
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "eeprom.h"
#include "hal/eeprom_24aa32a.h"

#define OFF_CAL_GRID 0x020
#define OFF_SDI_CONFIG 0x038
#define OFF_CAL_TABLES 0x0F4

static uint8_t storage[HAL_EEPROM_24AA32A_SIZE_BYTES];
static int fail_write_at = -1;
static unsigned read_calls;
static bool fail_reads;
void hal_eeprom_24aa32a_init(void) {}
bool hal_eeprom_24aa32a_read(uint16_t addr, uint8_t *data, size_t len) {
    read_calls++;
    if (fail_reads) return false;
    assert((size_t)addr + len <= sizeof(storage));
    memcpy(data, storage + addr, len);
    return true;
}
bool hal_eeprom_24aa32a_write(uint16_t addr, const uint8_t *data, size_t len) {
    assert((size_t)addr + len <= sizeof(storage));
    if (addr == fail_write_at) return false;
    memcpy(storage + addr, data, len);
    return true;
}

// Prefetch (no-op while the cache is valid), then interpolate from RAM.
static bool interpolate(int idx, uint64_t freq_hz, double *out_db) {
    eeprom_cal_prefetch();
    return eeprom_interpolate_cached(idx, freq_hz, out_db);
}

int main(void) {
    memset(storage, 0xff, sizeof(storage));
    assert(eeprom_init()); // blank: formatted
    assert(storage[4] == 3 && eeprom_hw_revision() == 1);
    const uint64_t f0 = 50000000ULL, f1 = 6000000000ULL;
    const double input[] = {-20.0, -10.0};
    double values[EEPROM_CAL_POINTS_MAX], correction;
    uint64_t got_f0, got_f1;
    size_t count;
    for (int idx = 0; idx < 8; ++idx) {
        assert(eeprom_cal_set(idx, f0, f1, input, 2) == EEPROM_CAL_OK);
        unsigned uncached_reads = read_calls;
        assert(!eeprom_interpolate_cached(idx, f0, &correction));
        assert(read_calls == uncached_reads);
        assert(eeprom_cal_get(idx, &got_f0, &got_f1, values, &count));
        assert(got_f0 == f0 && got_f1 == f1 && count == EEPROM_CAL_POINTS_MAX);
        assert(values[0] == -20.0 && values[count - 1] == -10.0);
        const uint64_t frequencies[] = {f0, 4294967295ULL, 4294967296ULL, 5000000000ULL, f1};
        for (size_t i = 0; i < sizeof(frequencies)/sizeof(frequencies[0]); ++i) {
            uint64_t f = frequencies[i];
            double expected = -20.0 + 10.0 * (double)(f - f0) / (double)(f1 - f0);
            assert(interpolate(idx, f, &correction));
            assert(fabs(correction - expected) <= 0.0051);
        }
        assert(!interpolate(idx, f0 - 1, &correction));
        assert(!interpolate(idx, f1 + 1, &correction));
    }
    // The last table ends inside the EEPROM.
    assert(OFF_CAL_TABLES + 8 * EEPROM_CAL_POINTS_MAX * 2 <= HAL_EEPROM_24AA32A_SIZE_BYTES);
    eeprom_cal_prefetch();
    unsigned before_reads = read_calls;
    for (int i=0; i<201; i++) for (int idx=0; idx<8; idx++)
        assert(interpolate(idx, f0 + (f1-f0)*(uint64_t)i/200, &correction));
    assert(read_calls == before_reads); // Whole sweep interpolation is RAM-only.
    assert(eeprom_cal_set(0, f0, f1 - 1, input, 2) == EEPROM_CAL_GRID_MISMATCH);
    assert(eeprom_init()); // Valid format must preserve all eight curves across a reboot.
    assert(eeprom_cal_get(7, &got_f0, &got_f1, values, &count));
    eeprom_cal_clear(0);
    assert(!eeprom_cal_get(0, &got_f0, &got_f1, values, &count));
    assert(!interpolate(0, f0, &correction));
    assert(eeprom_cal_get(7, &got_f0, &got_f1, values, &count));
    assert(eeprom_cal_clear_all());
    // A stored grid below 50 MHz is not used.
    assert(eeprom_cal_set(0, f0, f1, input, 2) == EEPROM_CAL_OK);
    const uint64_t low_f0 = 23500000ULL;
    memcpy(storage + OFF_CAL_GRID, &low_f0, sizeof(low_f0));
    assert(eeprom_init());
    assert(!eeprom_cal_get(0, &got_f0, &got_f1, values, &count));
    assert(!interpolate(0, 50000000ULL, &correction));
    assert(eeprom_cal_set(0, low_f0, f1, input, 2) == EEPROM_CAL_BAD_ARGS);
    assert(eeprom_cal_clear_all());
    // A grid entirely above UINT32_MAX must work too (not just its upper endpoint).
    assert(eeprom_cal_set(0, 5000000000ULL, f1, input, 2) == EEPROM_CAL_OK);
    assert(interpolate(0, 5500000000ULL, &correction));
    assert(fabs(correction + 15.0) < 0.0051);
    assert(eeprom_cal_set(0, f1, 5000000000ULL, input, 2) == EEPROM_CAL_BAD_ARGS);
    const double invalid[] = {400.0};
    assert(eeprom_cal_set(0, 5000000000ULL, f1, invalid, 1) == EEPROM_CAL_BAD_ARGS);
    fail_write_at = OFF_CAL_TABLES; // Existing table write fails after validity was cleared.
    assert(eeprom_cal_set(0, 5000000000ULL, f1, input, 2) == EEPROM_CAL_STORAGE_ERROR);
    assert(!eeprom_cal_get(0, &got_f0, &got_f1, values, &count));
    fail_write_at = -1;
    assert(!interpolate(0, 5500000000ULL, &correction));
    assert(eeprom_cal_set(0, 5000000000ULL, f1, input, 2) == EEPROM_CAL_OK);
    assert(interpolate(0, 5500000000ULL, &correction));

    // SDI points cover the whole range, 12G-SDI at 5.94 GHz included.
    sdi_test_point_t sdi[EEPROM_SDI_POINTS_MAX];
    for (size_t i = 0; i < EEPROM_SDI_POINTS_MAX; ++i)
        sdi[i] = (sdi_test_point_t){.freq_hz = 5940000000ULL - i, .thresh_db_x100 = (int16_t)(-1200 - (int)i),
                                    .cmp_gt = i & 1, .s_param = (uint8_t)(i % 4)};
    assert(eeprom_sdi_test_set(sdi, EEPROM_SDI_POINTS_MAX));
    sdi_test_point_t back[EEPROM_SDI_POINTS_MAX];
    eeprom_sdi_test_get(back, &count);
    assert(count == EEPROM_SDI_POINTS_MAX);
    for (size_t i = 0; i < count; ++i) {
        assert(back[i].freq_hz == sdi[i].freq_hz && back[i].thresh_db_x100 == sdi[i].thresh_db_x100);
        assert(back[i].cmp_gt == sdi[i].cmp_gt && back[i].s_param == sdi[i].s_param);
    }
    // The SDI block does not reach into the calibration tables.
    assert(interpolate(0, 5500000000ULL, &correction) && fabs(correction + 15.0) < 0.0051);

    storage[4] = 2; // A format-2 EEPROM is formatted on the first boot of format 3.
    assert(eeprom_init());
    assert(storage[4] == 3);
    assert(!eeprom_cal_get(0, &got_f0, &got_f1, values, &count));
    assert(!interpolate(0, 5500000000ULL, &correction));
    eeprom_sdi_test_get(back, &count);
    assert(count == 0);
    // A failed reset must not stamp a new-format header over stale config.
    storage[4] = 2;
    fail_write_at = OFF_SDI_CONFIG;
    assert(!eeprom_init()); assert(storage[4] == 2);
    fail_write_at = -1;
    assert(eeprom_init()); assert(storage[4] == 3);
    // An unreadable EEPROM is reported.
    fail_reads = true;
    assert(!eeprom_init() && eeprom_hw_revision() == 0);
    // A failed prefetch must not trigger I2C retries in measurement processing.
    eeprom_cal_prefetch();
    unsigned failed_prefetch_reads = read_calls;
    for (unsigned i = 0; i < 100; ++i)
        assert(!eeprom_interpolate_cached(0, f0, &correction));
    assert(read_calls == failed_prefetch_reads);
    fail_reads = false;
    puts("PASS: 6 GHz calibration, 6 GHz SDI points, RAM-only interpolation even after I2C failure, persistence, layout and format 3 migration");
    return 0;
}
