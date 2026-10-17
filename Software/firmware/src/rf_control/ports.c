// Block layer: port-select and impedance switches with their LEDs.
#include "rf_control/ports.h"

#include "hardware/gpio.h"

#include "hal/rf_switch.h"
#include "sna.h"

// Port-select and 50/75ohm impedance switches: SPDT. See the functions below
// for the C1/C2 truth table (opposite bits, not a "both bits same" gang).
static const hal_rf_switch_t sw_port = { GPIO_SW_PORT_C1, GPIO_SW_PORT_C2 };
static const hal_rf_switch_t sw_z = { GPIO_SW_Z_C1, GPIO_SW_Z_C2 };

void rf_ctrl_ports_init(void) {
    hal_rf_switch_init(&sw_port);
    hal_rf_switch_init(&sw_z);
}

// 0 = terminated (C1=C2=0: every input of the switch is terminated),
// 1/2 = route the source to that port. A plain opposite-bit SPDT select,
// same convention as rf_ctrl_z_switch_set():
//   src -> port1: C1=0, C2=1 (code 0b10)
//   src -> port2: C1=1, C2=0 (code 0b01)
// The port LED follows the switch, so it is updated here and nowhere else.
void rf_ctrl_port_switch_set(uint8_t port) {
    hal_rf_switch_set(&sw_port, port == 1 ? 0b10 : port == 2 ? 0b01 : 0b00);
    gpio_put(GPIO_LED_PORT1_OUT_EN, port == 1);
}

//   z50: C1=1, C2=0 (code 0b01)
//   z75: C1=0, C2=1 (code 0b10)
// The impedance LED follows the switch, so it is updated here and nowhere else.
// boards/sna.h flags the LED pin's polarity as unverified on hardware.
void rf_ctrl_z_switch_set(uint8_t z) {
    hal_rf_switch_set(&sw_z, z == 75 ? 0b10 : 0b01);
    gpio_put(GPIO_LED_PORT_Z50, z == 50);
}
