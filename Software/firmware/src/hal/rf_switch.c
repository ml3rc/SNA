#include "hal/rf_switch.h"

#include "hardware/gpio.h"

void hal_rf_switch_init(const hal_rf_switch_t *sw) {
    gpio_init(sw->gpio_c1);
    gpio_set_dir(sw->gpio_c1, GPIO_OUT);
    gpio_init(sw->gpio_c2);
    gpio_set_dir(sw->gpio_c2, GPIO_OUT);
}

void hal_rf_switch_set(const hal_rf_switch_t *sw, uint8_t code) {
    // Both lines in one SIO write. Two separate writes would show an
    // intermediate code for a moment: the port switch going 2 -> 1 would pass
    // through 0b00 (terminated, with the attenuator already released) and
    // 1 -> 2 through the undefined 0b11.
    uint64_t mask = (1ull << sw->gpio_c1) | (1ull << sw->gpio_c2);
    uint64_t value = ((uint64_t) (code & 0x1) << sw->gpio_c1) | ((uint64_t) ((code >> 1) & 0x1) << sw->gpio_c2);
    gpio_put_masked64(mask, value);
}
