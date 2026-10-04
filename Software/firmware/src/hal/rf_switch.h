#ifndef SNA_HAL_RF_SWITCH_H
#define SNA_HAL_RF_SWITCH_H

#include <stdint.h>

#include "hardware/gpio.h" // for enum gpio_drive_strength
#include "pico/types.h" // for `uint`

// Generic driver for this board's RF switches (Qorvo QPC6324 SPDT and
// RFSW6042 SP4T) -- both are controlled the same way from the digital side:
// a pair of logic lines selecting among the switch's positions.
//
// `code` bit 0 drives c1, bit 1 drives c2. An SP4T switch (2 independent
// control lines, 4 positions) uses the full 2-bit code. An SPDT switch only
// has one real select line electrically, but on this board each SPDT
// position is wired to two or three physical ICs in parallel sharing one
// logical control signal pair -- the board layer selects a position by
// driving both bits together (0x0 or 0x3), not by using bit 1 independently.
// See Software/protocol/PINMAP.md for which instance is which.
typedef struct {
    uint gpio_c1;
    uint gpio_c2;
} rf_switch_t;

void rf_switch_init(const rf_switch_t *sw);
void rf_switch_set(const rf_switch_t *sw, uint8_t code);

// RP2350 pads default to 4mA nominal drive strength on reset; this lets the
// board layer raise it (2/4/8/12mA available) for specific switch
// instances, e.g. ones with heavier load or faster-settling requirements.
void rf_switch_set_drive_strength(const rf_switch_t *sw, enum gpio_drive_strength drive);

#endif
