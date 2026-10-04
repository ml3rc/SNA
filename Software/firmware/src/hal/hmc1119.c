#include "hal/hmc1119.h"

#include <stddef.h>

#include "hardware/gpio.h"
#include "pico/stdlib.h"

#include "sna.h"

void hmc1119_init(void) {
    const uint pins[] = {
        GPIO_ATTEN_D0, GPIO_ATTEN_D1, GPIO_ATTEN_D2, GPIO_ATTEN_D3,
        GPIO_ATTEN_D4, GPIO_ATTEN_D5, GPIO_ATTEN_D6, GPIO_ATTEN_LE,
    };
    for (size_t i = 0; i < count_of(pins); i++) {
        gpio_init(pins[i]);
        gpio_set_dir(pins[i], GPIO_OUT);
    }
}

void hmc1119_set_atten(uint8_t value7bit) {
    gpio_put(GPIO_ATTEN_D0, (value7bit >> 0) & 1);
    gpio_put(GPIO_ATTEN_D1, (value7bit >> 1) & 1);
    gpio_put(GPIO_ATTEN_D2, (value7bit >> 2) & 1);
    gpio_put(GPIO_ATTEN_D3, (value7bit >> 3) & 1);
    gpio_put(GPIO_ATTEN_D4, (value7bit >> 4) & 1);
    gpio_put(GPIO_ATTEN_D5, (value7bit >> 5) & 1);
    gpio_put(GPIO_ATTEN_D6, (value7bit >> 6) & 1);
    gpio_put(GPIO_ATTEN_LE, 1);
    sleep_us(1);
    gpio_put(GPIO_ATTEN_LE, 0);
}
