#ifndef SNA_HAL_HMC1119_H
#define SNA_HAL_HMC1119_H

#include <stdint.h>

// Analog Devices HMC1119 digital step attenuator (U401, ~0..-32dB,
// 0.1-6GHz), controlled via a 7-bit parallel word (D0-D6) latched on LE --
// not SPI. See Software/docs/firmware/protocol/PINMAP.md.

void hal_hmc1119_init(void);
void hal_hmc1119_set_atten(uint8_t value7bit); // 0-127

#endif
