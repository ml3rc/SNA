#ifndef TEST_I2C_H
#define TEST_I2C_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
typedef struct { int unused; } i2c_inst_t;
extern i2c_inst_t test_i2c;
#define i2c0 (&test_i2c)
unsigned int i2c_init(i2c_inst_t *i2c, unsigned int baud);
int i2c_write_blocking(i2c_inst_t *i2c, uint8_t address, const uint8_t *buf, size_t len, bool nostop);
int i2c_read_blocking(i2c_inst_t *i2c, uint8_t address, uint8_t *buf, size_t len, bool nostop);
int i2c_write_timeout_us(i2c_inst_t *i2c, uint8_t address, const uint8_t *buf, size_t len, bool nostop, unsigned int timeout_us);
int i2c_read_timeout_us(i2c_inst_t *i2c, uint8_t address, uint8_t *buf, size_t len, bool nostop, unsigned int timeout_us);
#endif
