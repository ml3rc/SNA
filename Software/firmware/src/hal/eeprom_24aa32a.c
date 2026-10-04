#include "hal/eeprom_24aa32a.h"

#include <string.h>

#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "pico/stdlib.h"

#include "sna.h"

void eeprom_24aa32a_init(void) {
    i2c_init(I2C_PORT, I2C_BAUD);
    gpio_set_function(GPIO_I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(GPIO_I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(GPIO_I2C_SDA);
    gpio_pull_up(GPIO_I2C_SCL);
}

bool eeprom_24aa32a_write(uint16_t addr, const uint8_t *data, size_t len) {
    if ((size_t) addr + len > EEPROM_24AA32A_SIZE_BYTES) return false;
    while (len > 0) {
        size_t page_offset = addr % EEPROM_24AA32A_PAGE_SIZE;
        size_t chunk = EEPROM_24AA32A_PAGE_SIZE - page_offset;
        if (chunk > len) chunk = len;

        uint8_t buf[2 + EEPROM_24AA32A_PAGE_SIZE];
        buf[0] = (uint8_t) (addr >> 8);
        buf[1] = (uint8_t) (addr & 0xFF);
        memcpy(&buf[2], data, chunk);

        int ret = i2c_write_blocking(I2C_PORT, EEPROM_I2C_ADDR, buf, 2 + chunk, false);
        if (ret != (int) (2 + chunk)) return false;

        sleep_ms(5); // 24AA32A datasheet write-cycle time

        addr = (uint16_t) (addr + chunk);
        data += chunk;
        len -= chunk;
    }
    return true;
}

bool eeprom_24aa32a_read(uint16_t addr, uint8_t *data, size_t len) {
    if ((size_t) addr + len > EEPROM_24AA32A_SIZE_BYTES) return false;
    uint8_t addr_buf[2] = { (uint8_t) (addr >> 8), (uint8_t) (addr & 0xFF) };
    int ret = i2c_write_blocking(I2C_PORT, EEPROM_I2C_ADDR, addr_buf, 2, true);
    if (ret != 2) return false;
    ret = i2c_read_blocking(I2C_PORT, EEPROM_I2C_ADDR, data, len, false);
    return ret == (int) len;
}
