#ifndef SNA_HAL_TRF37A73_H
#define SNA_HAL_TRF37A73_H

#include <stdbool.h>

// Texas Instruments TRF37A73 RF gain block amplifier (U402, +12dB,
// 0.1-6GHz) in the RF-Attenuation/Amp stage. PWDN is active-high (pin high
// disables the amp, low enables it). It has an internal pulldown on PWDN
// and there's no external pull resistor on this board's net for it -- so
// with no firmware running, PWDN floats low and the amp is enabled by
// default (unlike max2871's CE, which has an external pull-down and
// defaults safely off). See Software/docs/firmware/protocol/PINMAP.md.

void hal_trf37a73_init(void);
void hal_trf37a73_enable(bool en);

#endif
