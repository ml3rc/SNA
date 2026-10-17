#ifndef SNA_RF_CONTROL_STATE_H
#define SNA_RF_CONTROL_STATE_H

#include <stdbool.h>
#include <stdint.h>

// Types and constants of the block layer that callers of this layer need.
#include "rf_control/coupler.h"  // rf_ctrl_coupler_path_t
#include "rf_control/detector.h" // rf_ctrl_detector_sample_t, RF_CTRL_DETECTOR_*
#include "rf_control/source.h"   // RF_CTRL_LPF_*

// State layer: the only caller of the block layer (source/coupler/detector/
// ports), and so the only code that changes RF hardware. It holds the current
// configuration (source state, port, impedance, attenuation, frequency, LPF,
// coupler path) and applies every change in a safe order:
//   - parking: attenuator to RF_STATE_PARKED_ATTEN first, then terminate the port switch;
//     switching it on is the reverse (switch first, then release the attenuator);
//   - the MAX2871 power-up double write after every CE low->high.
// It knows nothing about sweeps, the generator or other applications; those
// decide WHEN to change something (application.c), this layer decides HOW.

#define RF_STATE_DEFAULT_Z 50
#define RF_STATE_DEFAULT_PORT 1
#define RF_STATE_PARKED_ATTEN 127
#define RF_STATE_SOURCE_BOOT_DELAY_MS 1000 // source held off after boot before its first power-up

// Initializes every RF block and leaves the source PARKED (MAX2871 and amp
// running, tuned to SNA_FREQ_DEFAULT with the power-up double write), port
// switch terminated, port 1 remembered, 50 ohm, attenuator 127, LPF for
// SNA_FREQ_DEFAULT, coupler path port 1 forward, detector on.
void rf_state_init(void);

// --- Source: MAX2871 + amplifier + attenuator, always controlled together ---
//   OFF     source and amp powered down, port switch terminated, attenuator 127
//   PARKED  source and amp running, port switch terminated, attenuator 127
//   ON      source and amp running, output on rf_state_get_port(), attenuator at rf_state_get_atten()
// Leaving OFF powers the MAX2871 up; the next rf_state_set_freq() then does
// the datasheet power-up double write.
typedef enum {
    RF_STATE_SOURCE_OFF,
    RF_STATE_SOURCE_PARKED,
    RF_STATE_SOURCE_ON,
    RF_STATE_SOURCE_NONE,
} rf_state_source_t;
// False (nothing changed) for OFF -> ON: the source would be routed out before
// it is powered and programmed. Go through PARKED and rf_state_set_freq()
// (the power-up double write) first. Also false for an invalid state.
bool rf_state_set_source(rf_state_source_t state);
rf_state_source_t rf_state_get_source(void);

// 1 or 2. Remembers the port; switches it only while the source is
// RF_STATE_SOURCE_ON (rf_state_set_source(ON) switches it otherwise).
// Returns whether the source is on now. To switch the source on: set the port
// first, then rf_state_set_source(RF_STATE_SOURCE_ON) -- the other order would
// briefly switch to the previous port with the attenuation already released.
// Switching while ON goes directly from one port to the other: both switch
// lines change in one GPIO write (hal_rf_switch_set()), never via "terminated".
bool rf_state_set_port(uint8_t port);
uint8_t rf_state_get_port(void);

// 50 or 75; false (ignored) otherwise. Switches immediately, also while the
// source is on (hot switching is allowed).
bool rf_state_set_z(uint8_t z);
uint8_t rf_state_get_z(void);

// 0-127 (larger values clamp to 127). Stores the requested code; it only
// reaches the hardware while the source is ON, otherwise the attenuator is
// held at RF_STATE_PARKED_ATTEN.
void rf_state_set_atten(uint8_t value7bit);
uint8_t rf_state_get_atten(void);

// Tunes the MAX2871 (single register write set). The first call after a CE
// power-up writes twice, >= 20 ms apart (datasheet). False outside
// SNA_FREQ_MIN_HZ..SNA_FREQ_MAX_HZ.
bool rf_state_set_freq(uint64_t freq_hz);
uint64_t rf_state_get_freq(void); // last frequency set successfully
bool rf_state_source_locked(void); // LD pin
bool rf_state_read_temp_c(double *temp_c_out); // rewrites MAX2871 registers 2/3/5 briefly

// LPF bank, RF_CTRL_LPF_NONE..RF_CTRL_LPF_947M; false (ignored) otherwise.
bool rf_state_set_lpf(uint8_t index);
// Harmonic-suppression LPF: the lowest filter whose cutoff is still >= freq_hz
// (RF_CTRL_LPF_FREQ_*): 947 MHz LPF up to 947 MHz, 1747 MHz up to 1747 MHz,
// 3500 MHz up to 3500 MHz, no filter above. Using each filter up to its cutoff
// measured better than a 0.8 margin; moving the edges invalidates stored calibration.
void rf_state_set_lpf_for_freq(uint64_t freq_hz);
uint8_t rf_state_get_lpf(void);

// Coupler bank, one of the four defined paths; false (ignored) otherwise.
bool rf_state_set_coupler_path(rf_ctrl_coupler_path_t path);
rf_ctrl_coupler_path_t rf_state_get_coupler_path(void);

// Detector (always on): RF_CTRL_DETECTOR_SAMPLES conversions as median/min/max.
// No settle wait: the caller waits after switching.
rf_ctrl_detector_sample_t rf_state_detector_sample(void);

#endif
