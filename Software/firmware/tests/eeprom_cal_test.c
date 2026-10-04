// Host-only regression test. The EEPROM HAL is backed by RAM; no hardware is accessed.
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "eeprom.h"
#include "hal/eeprom_24aa32a.h"

static uint8_t storage[EEPROM_24AA32A_SIZE_BYTES];
static int fail_write_at = -1;
static unsigned read_calls;
static bool fail_reads;
void eeprom_24aa32a_init(void) {}
bool eeprom_24aa32a_read(uint16_t addr, uint8_t *data, size_t len) {
    read_calls++;
    if (fail_reads) return false;
    assert((size_t)addr + len <= sizeof(storage));
    memcpy(data, storage + addr, len);
    return true;
}
bool eeprom_24aa32a_write(uint16_t addr, const uint8_t *data, size_t len) {
    assert((size_t)addr + len <= sizeof(storage));
    if (addr == fail_write_at) return false;
    memcpy(storage + addr, data, len);
    return true;
}

int main(void) {
    memset(storage, 0xff, sizeof(storage));
    eeprom_init();
    const uint64_t f0 = 50000000ULL, f1 = 6000000000ULL;
    const double input[] = {-20.0, -10.0};
    double values[EEPROM_CAL_POINTS_MAX], correction;
    uint64_t got_f0, got_f1;
    size_t count;
    for (int idx = 0; idx < 8; ++idx) {
        assert(eeprom_cal_set(idx, f0, f1, input, 2));
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
            assert(eeprom_interpolate(idx, f, &correction));
            assert(fabs(correction - expected) <= 0.0051);
        }
        assert(!eeprom_interpolate(idx, f0 - 1, &correction));
        assert(!eeprom_interpolate(idx, f1 + 1, &correction));
    }
    eeprom_cal_prefetch();
    unsigned before_reads = read_calls;
    for (int i=0; i<201; i++) for (int idx=0; idx<8; idx++)
        assert(eeprom_interpolate(idx, f0 + (f1-f0)*(uint64_t)i/200, &correction));
    assert(read_calls == before_reads); // Whole sweep interpolation is RAM-only.
    assert(!eeprom_cal_set(0, f0, f1 - 1, input, 2));
    eeprom_init(); // Valid format must preserve all eight curves across a reboot.
    assert(eeprom_cal_get(7, &got_f0, &got_f1, values, &count));
    uint8_t before[sizeof(storage)];
    memcpy(before, storage, sizeof(storage));
    assert(eeprom_self_test());
    assert(memcmp(before, storage, 4024) == 0);
    eeprom_cal_clear(0);
    assert(!eeprom_cal_get(0, &got_f0, &got_f1, values, &count));
    assert(!eeprom_interpolate(0, f0, &correction));
    assert(eeprom_cal_get(7, &got_f0, &got_f1, values, &count));
    eeprom_cal_clear_all();
    // Existing format-2 calibration below 50 MHz remains readable without a wipe.
    assert(eeprom_cal_set(0, f0, f1, input, 2));
    const uint64_t legacy_f0 = 23500000ULL;
    memcpy(storage + 0x020, &legacy_f0, sizeof(legacy_f0));
    eeprom_init();
    assert(eeprom_cal_get(0, &got_f0, &got_f1, values, &count));
    assert(got_f0 == legacy_f0);
    assert(eeprom_interpolate(0, 50000000ULL, &correction));
    assert(!eeprom_interpolate(0, legacy_f0, &correction));
    assert(!eeprom_cal_set(0, legacy_f0, f1, input, 2));
    assert(eeprom_cal_clear_all());
    // A grid entirely above UINT32_MAX must work too (not just its upper endpoint).
    assert(eeprom_cal_set(0, 5000000000ULL, f1, input, 2));
    assert(eeprom_interpolate(0, 5500000000ULL, &correction));
    assert(fabs(correction + 15.0) < 0.0051);
    assert(!eeprom_cal_set(0, f1, 5000000000ULL, input, 2));
    const double invalid[] = {400.0};
    assert(!eeprom_cal_set(0, 5000000000ULL, f1, invalid, 1));
    fail_write_at = 0x0B8; // Existing table write fails after validity was cleared.
    assert(!eeprom_cal_set(0, 5000000000ULL, f1, input, 2));
    assert(!eeprom_cal_get(0, &got_f0, &got_f1, values, &count));
    fail_write_at = -1;
    assert(!eeprom_interpolate(0, 5500000000ULL, &correction));
    assert(eeprom_cal_set(0, 5000000000ULL, f1, input, 2));
    assert(eeprom_interpolate(0, 5500000000ULL, &correction));
    storage[4] = 1; // Old format is invalidated on the first boot of format 2.
    eeprom_init();
    assert(storage[4] == 2);
    assert(!eeprom_cal_get(0, &got_f0, &got_f1, values, &count));
    assert(!eeprom_interpolate(0, 5500000000ULL, &correction));
    sdi_test_point_t sdi[EEPROM_SDI_POINTS_MAX];
    eeprom_sdi_test_get(sdi, &count);
    assert(count == 0);
    // A failed reset must not stamp a new-format header over stale config.
    storage[4] = 1;
    fail_write_at = 0x038;
    eeprom_init(); assert(storage[4] == 1);
    fail_write_at = -1;
    eeprom_init(); assert(storage[4] == 2);
    // A failed prefetch must not trigger I2C retries in acquisition processing.
    eeprom_init();
    fail_reads = true;
    eeprom_cal_prefetch();
    unsigned failed_prefetch_reads = read_calls;
    for (unsigned i = 0; i < 100; ++i)
        assert(!eeprom_interpolate_cached(0, f0, &correction));
    assert(read_calls == failed_prefetch_reads);
    fail_reads = false;
    puts("PASS: 6 GHz calibration, RAM-only acquisition even after I2C failure, persistence, layout and format migration");
    return 0;
}
