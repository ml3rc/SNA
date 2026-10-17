#ifndef SNA_HAL_RF_SWITCH_H
#define SNA_HAL_RF_SWITCH_H

#include <stdint.h>

#include "pico/types.h" // for `uint`

// Generic driver for this board's RF switches (Qorvo QPC6324 SPDT and
// RFSW6042 SP4T) -- both are controlled the same way from the digital side:
// a pair of logic lines selecting among the switch's positions.
//
// `code` bit 0 drives c1, bit 1 drives c2. An SP4T switch (2 independent
// control lines, 4 positions) uses the full 2-bit code. The SPDT port and
// impedance switches are driven with opposite bits (0b01 / 0b10) to select a
// side, and the port switch uses 0b00 to terminate every input -- see
// rf_control/ports.c for the codes.
// See Software/protocol/PINMAP.md for which instance is which.
typedef struct {
    uint gpio_c1;
    uint gpio_c2;
} hal_rf_switch_t;

void hal_rf_switch_init(const hal_rf_switch_t *sw);
// Sets c1 and c2 in the same instant (one GPIO write), never through another code.
void hal_rf_switch_set(const hal_rf_switch_t *sw, uint8_t code);

// Each RFSW6042TR7's 2-bit code -> which of its 4 RF legs is selected
// (confirmed on the board; Qorvo doesn't publish a truth table for this part):
//   C1 C2 -> leg (pkg pin)
//    1  0 -> RF1 (9)
//    0  1 -> RF2 (11)
//    1  1 -> RF3 (6)
//    0  0 -> RF4 (4)
// hal_rf_switch_set()'s `code` packs C1 in bit0, C2 in bit1, so:
#define HAL_RF_LEG_1 1 /* C1=1,C2=0 */
#define HAL_RF_LEG_2 2 /* C1=0,C2=1 */
#define HAL_RF_LEG_3 3 /* C1=1,C2=1 */
#define HAL_RF_LEG_4 0 /* C1=0,C2=0 */

#endif
