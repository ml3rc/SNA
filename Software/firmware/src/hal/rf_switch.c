#include "hal/rf_switch.h"

#include "hardware/gpio.h"

void rf_switch_init(const rf_switch_t *sw) {
    gpio_init(sw->gpio_c1);
    gpio_set_dir(sw->gpio_c1, GPIO_OUT);
    gpio_init(sw->gpio_c2);
    gpio_set_dir(sw->gpio_c2, GPIO_OUT);
}

void rf_switch_set(const rf_switch_t *sw, uint8_t code) {
    gpio_put(sw->gpio_c1, code & 0x1);
    gpio_put(sw->gpio_c2, (code >> 1) & 0x1);
}

void rf_switch_set_drive_strength(const rf_switch_t *sw, enum gpio_drive_strength drive) {
    gpio_set_drive_strength(sw->gpio_c1, drive);
    gpio_set_drive_strength(sw->gpio_c2, drive);
}
