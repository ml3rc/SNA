#ifndef SNA_RF_CONTROL_SOURCE_H
#define SNA_RF_CONTROL_SOURCE_H

#include <stdbool.h>
#include <stdint.h>

// Block layer: RF source chain -- MAX2871 PLL/VCO, LPF bank, HMC1119 step
// attenuator and TRF37A73 amplifier. Only this file talks to their HAL modules.
// Plain hardware operations: sequencing (power-up double write, parking order,
// filter choice) is the state layer's job (state.c).

// LPF bank positions (rf_ctrl_lpf_select()).
#define RF_CTRL_LPF_NONE  0u
#define RF_CTRL_LPF_3500M 1u
#define RF_CTRL_LPF_1747M 2u
#define RF_CTRL_LPF_947M  3u

// Highest frequency each filter is used for: its cutoff. Measured on the board:
// a multiplier of 1.0 on the cutoff gave better results than 0.8 and others.
#define RF_CTRL_LPF_FREQ_947M  947000000ULL
#define RF_CTRL_LPF_FREQ_1747M 1747000000ULL
#define RF_CTRL_LPF_FREQ_3500M 3500000000ULL

void rf_ctrl_source_init(void);

// MAX2871 and amplifier power, always switched together.
void rf_ctrl_source_power(bool on);

// HMC1119 code, 0.25 dB per step, RF_CTRL_ATTEN_MAX = maximum attenuation.
// Larger codes are clamped: the chip takes 7 bits, so 128 would wrap to 0,
// the maximum output.
#define RF_CTRL_ATTEN_MAX 127u
void rf_ctrl_atten_write(uint8_t code);

// One register write set, see hal/max2871.c for the register math. False
// outside SNA_FREQ_MIN_HZ..SNA_FREQ_MAX_HZ.
bool rf_ctrl_source_set_freq(uint64_t freq_hz);
bool rf_ctrl_source_locked(void); // LD pin
bool rf_ctrl_source_read_temp_c(double *temp_c_out); // on-chip temp sensor -- see hal/max2871.c

// RF_CTRL_LPF_NONE..RF_CTRL_LPF_947M; false (nothing switched) for other values.
bool rf_ctrl_lpf_select(uint8_t index);

#endif
