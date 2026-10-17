#ifndef SNA_RF_CONTROL_PORTS_H
#define SNA_RF_CONTROL_PORTS_H

#include <stdint.h>

// Block layer: port-select switch and 50/75 ohm impedance switch, each with
// its on-board LED. Called only by the state layer (rf_state_set_source(),
// rf_state_set_port(), rf_state_set_z()), which keeps the
// attenuate-then-terminate ordering and the remembered port/impedance.

void rf_ctrl_ports_init(void);

// 0 = terminated, 1/2 = source on at that port. Also drives PORT1_OUT_EN.
void rf_ctrl_port_switch_set(uint8_t port);
// 50 or 75 ohm. Also drives PORT_Z50.
void rf_ctrl_z_switch_set(uint8_t z);

#endif
