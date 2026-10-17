#include "hal/eeprom_24aa32a.h"

#include <string.h>

#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "pico/stdlib.h"

#include "sna.h"

#define HAL_EEPROM_24AA32A_WRITE_CYCLE_TIME 5
// Per transfer: generous for 400 kHz (a byte takes about 23 us), so only a bus held
// by a stuck slave ever hits it -- without it the blocking SDK calls never return.
#define HAL_EEPROM_24AA32A_TIMEOUT_US(len) (2000u + 100u * (uint32_t) (len))
#define HAL_EEPROM_24AA32A_RECOVERY_CLOCKS 9
#define HAL_EEPROM_24AA32A_RECOVERY_HALF_PERIOD_US 5

// A reset in the middle of a read (watchdog, bootsel, SWD) can leave the EEPROM
// driving SDA low, waiting for clocks that never come; the controller then never
// gets the bus. Up to nine SCL pulses let it finish its byte and release SDA.
static void bus_recover(void) {
    gpio_init(GPIO_I2C_SDA);
    gpio_set_dir(GPIO_I2C_SDA, GPIO_IN);
    gpio_pull_up(GPIO_I2C_SDA);
    gpio_init(GPIO_I2C_SCL);
    gpio_pull_up(GPIO_I2C_SCL);
    gpio_set_dir(GPIO_I2C_SCL, GPIO_IN); // released: the pull-up holds it high
    for (int i = 0; i < HAL_EEPROM_24AA32A_RECOVERY_CLOCKS && !gpio_get(GPIO_I2C_SDA); i++) {
        // Open-drain style: drive low, or release to the pull-up.
        gpio_put(GPIO_I2C_SCL, false);
        gpio_set_dir(GPIO_I2C_SCL, GPIO_OUT);
        sleep_us(HAL_EEPROM_24AA32A_RECOVERY_HALF_PERIOD_US);
        gpio_set_dir(GPIO_I2C_SCL, GPIO_IN);
        sleep_us(HAL_EEPROM_24AA32A_RECOVERY_HALF_PERIOD_US);
    }
}

void hal_eeprom_24aa32a_init(void) {
    bus_recover();
    i2c_init(I2C_PORT, I2C_BAUD);
    gpio_set_function(GPIO_I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(GPIO_I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(GPIO_I2C_SDA);
    gpio_pull_up(GPIO_I2C_SCL);
}

bool hal_eeprom_24aa32a_write(uint16_t addr, const uint8_t *data, size_t len) {
    if ((size_t) addr + len > HAL_EEPROM_24AA32A_SIZE_BYTES) return false;
    while (len > 0) {
        size_t page_offset = addr % HAL_EEPROM_24AA32A_PAGE_SIZE;
        size_t chunk = HAL_EEPROM_24AA32A_PAGE_SIZE - page_offset;
        if (chunk > len) chunk = len;

        uint8_t buf[2 + HAL_EEPROM_24AA32A_PAGE_SIZE];
        buf[0] = (uint8_t) (addr >> 8);
        buf[1] = (uint8_t) (addr & 0xFF);
        memcpy(&buf[2], data, chunk);

        int ret = i2c_write_timeout_us(I2C_PORT, EEPROM_I2C_ADDR, buf, 2 + chunk, false,
            HAL_EEPROM_24AA32A_TIMEOUT_US(2 + chunk));
        if (ret != (int) (2 + chunk)) return false;

        sleep_ms(HAL_EEPROM_24AA32A_WRITE_CYCLE_TIME); // 24AA32A datasheet write-cycle time

        addr = (uint16_t) (addr + chunk);
        data += chunk;
        len -= chunk;
    }
    return true;
}

bool hal_eeprom_24aa32a_read(uint16_t addr, uint8_t *data, size_t len) {
    if ((size_t) addr + len > HAL_EEPROM_24AA32A_SIZE_BYTES) return false;
    uint8_t addr_buf[2] = { (uint8_t) (addr >> 8), (uint8_t) (addr & 0xFF) };
    int ret = i2c_write_timeout_us(I2C_PORT, EEPROM_I2C_ADDR, addr_buf, 2, true, HAL_EEPROM_24AA32A_TIMEOUT_US(2));
    if (ret != 2) return false;
    ret = i2c_read_timeout_us(I2C_PORT, EEPROM_I2C_ADDR, data, len, false, HAL_EEPROM_24AA32A_TIMEOUT_US(len));
    return ret == (int) len;
}
