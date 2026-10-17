#ifndef SNA_HAL_EEPROM_24AA32A_H
#define SNA_HAL_EEPROM_24AA32A_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Microchip 24AA32A I2C EEPROM (U203, 32Kbit/4096 bytes) -- raw chip-level
// byte read/write only. The structured calibration/config layout on top of
// this (header, cal grid, SDI thresholds, 8 calibration tables) lives one
// level up, in eeprom.h/eeprom.c (per Software/protocol/
// EEPROM_LAYOUT.md), not here.

#define HAL_EEPROM_24AA32A_SIZE_BYTES 4096
#define HAL_EEPROM_24AA32A_PAGE_SIZE  32

void hal_eeprom_24aa32a_init(void); // I2C0 peripheral + pin setup
bool hal_eeprom_24aa32a_read(uint16_t addr, uint8_t *data, size_t len);

// Handles page-boundary splitting and the datasheet write-cycle delay
// internally -- callers don't need to think about the 32-byte page size.
bool hal_eeprom_24aa32a_write(uint16_t addr, const uint8_t *data, size_t len);

#endif
