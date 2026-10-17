#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "hardware/i2c.h"
#include "hal/eeprom_24aa32a.h"

i2c_inst_t test_i2c;
static uint8_t storage[4096];
static uint16_t cursor;
static int short_write, short_read;
static unsigned calls;
unsigned int i2c_init(i2c_inst_t *i2c, unsigned int baud) { (void)i2c; return baud; }
void gpio_set_function(unsigned int pin, unsigned int function) { (void)pin; (void)function; }
void gpio_pull_up(unsigned int pin) { (void)pin; }
void gpio_init(unsigned int pin) { (void)pin; }
void gpio_set_dir(unsigned int pin, bool out) { (void)pin; (void)out; }
void gpio_put(unsigned int pin, bool value) { (void)pin; (void)value; }
static bool sda_stuck; // SDA reads low until a few SCL pulses
static unsigned scl_pulses;
bool gpio_get(unsigned int pin) { (void)pin; return !sda_stuck || scl_pulses >= 3; }
void sleep_ms(uint32_t ms) { (void)ms; }
void sleep_us(uint64_t us) { (void)us; ++scl_pulses; }
int i2c_write_timeout_us(i2c_inst_t *i2c, uint8_t addr, const uint8_t *buf, size_t len, bool nostop, unsigned int timeout_us) {
    (void)i2c; assert(timeout_us >= 2000); assert(addr == 0x50); assert(len >= 2); ++calls;
    if (short_write) return (int)len - 1;
    cursor = (uint16_t)(buf[0] << 8 | buf[1]);
    if (nostop) assert(len == 2);
    else {
        assert(cursor % 32 + len - 2 <= 32); // never wrap a page write
        assert((size_t)cursor + len - 2 <= sizeof(storage));
        memcpy(storage + cursor, buf + 2, len - 2);
    }
    return (int)len;
}
int i2c_read_timeout_us(i2c_inst_t *i2c, uint8_t addr, uint8_t *buf, size_t len, bool nostop, unsigned int timeout_us) {
    (void)i2c; assert(timeout_us >= 2000 + 100 * len); assert(addr == 0x50 && !nostop); ++calls;
    if (short_read) return (int)len - 1;
    assert((size_t)cursor + len <= sizeof(storage));
    memcpy(buf, storage + cursor, len); return (int)len;
}
int main(void) {
    // A slave holding SDA low after a reset gets clocked free before the bus is used.
    sda_stuck = true;
    hal_eeprom_24aa32a_init();
    assert(scl_pulses >= 3 && scl_pulses <= 2 * 9);
    sda_stuck = false;
    uint8_t data[70], readback[70];
    memset(data, 0xa5, sizeof(data));
    assert(hal_eeprom_24aa32a_write(31, data, sizeof(data)));
    assert(calls == 4); // 1, 32, 32, 5 bytes
    assert(hal_eeprom_24aa32a_read(31, readback, sizeof(readback)));
    assert(memcmp(data, readback, sizeof(data)) == 0);
    short_write = 1;
    assert(!hal_eeprom_24aa32a_write(0, data, 1));
    assert(!hal_eeprom_24aa32a_read(0, readback, 1));
    short_write = 0; short_read = 1;
    assert(!hal_eeprom_24aa32a_read(0, readback, 1));
    unsigned before = calls;
    assert(!hal_eeprom_24aa32a_write(4095, data, 2));
    assert(!hal_eeprom_24aa32a_read(4095, readback, 2));
    assert(calls == before);
    puts("PASS: real EEPROM HAL page splitting, short transfers and bounds");
    return 0;
}
