#ifndef SNA_HAL_AD8318_H
#define SNA_HAL_AD8318_H

#include <stdbool.h>
#include <stdint.h>

// Analog Devices AD8318 log detector (U507) -- converts the coupled RF
// signal to a DC voltage proportional to power in dB, read back via the
// RP2350's onboard ADC. RF_DET_EN has an external pull-down (R536, per
// Dokumentation.docx) so it defaults disabled with no firmware running.

void ad8318_init(void); // also does the one-time RP2350 adc_init() -- see ad8318.c
void ad8318_enable(bool en);
uint16_t ad8318_read_raw(void); // 12-bit ADC counts, raw (no dB conversion yet)

#endif
