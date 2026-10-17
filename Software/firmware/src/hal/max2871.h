#ifndef SNA_HAL_MAX2871_H
#define SNA_HAL_MAX2871_H

#include <stdbool.h>
#include <stdint.h>

// Analog Devices/Maxim MAX2871 wideband PLL/VCO synthesizer (U301), driven
// via a bit-banged 4-wire interface (CLK/DATA/LE) plus CE/RFOUT_EN/MUX/LD.
// See Software/protocol/PINMAP.md for the pin map this binds to.

void hal_max2871_init(void);

// CE has an external 10k pull-down (R303, see PINMAP.md) so the chip
// defaults disabled with no
// firmware running. This drives CE and RFOUT_EN together; the only caller
// is rf_ctrl_source_power() (rf_control/source.c), which switches the amplifier
// with it.
void hal_max2871_enable(bool en);

bool hal_max2871_locked(void); // LD pin: high = locked, low = unlocked

// See hal/max2871.c for the register math (N/FRAC/DIVA computation from the
// real MAX2871 datasheet) and board-specific constants (100MHz reference,
// RSET=5.1kOhm, from the "RF Source" schematic sheet). Returns false for
// freq_hz outside [23.5MHz, 6000MHz].
bool hal_max2871_set_freq(uint64_t freq_hz);

// Reads back register 6's DIE[3:0] field via the MUX pin used as MISO
// (datasheet "Read Sequence" -- the MUX pin is routed on the PCB for
// exactly this). 0111 = MAX2871, 0110 = MAX2870
// (pin/register compatible, so this is a real check against the wrong part
// being populated, not just a tautology). Safe to call regardless of PLL
// lock state -- see hal/max2871.c for why.
uint8_t hal_max2871_read_die_id(void);

// Reads the on-chip temperature sensor (datasheet "Temperature Sensor"
// section) via the same MUX-as-MISO register-6 readback as
// hal_max2871_read_die_id(). Returns false if the ADC reading comes back
// invalid (ADCV=0 -- e.g. CE is low/chip not actually enabled). Most
// accurate per the datasheet when the VCO is locked and RFOUTA is at full
// power; returned regardless, just less trustworthy otherwise.
bool hal_max2871_read_temp_c(double *temp_c_out);

#endif
