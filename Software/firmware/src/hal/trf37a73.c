#include "hal/trf37a73.h"

#include "hardware/gpio.h"

#include "sna.h"

void hal_trf37a73_init(void) {
    gpio_init(GPIO_RF_AMP_PWDN);
    gpio_put(GPIO_RF_AMP_PWDN, 1); // establish disabled level before enabling the output
    gpio_set_dir(GPIO_RF_AMP_PWDN, GPIO_OUT);
}

void hal_trf37a73_enable(bool en) {
    // Active-high PWDN, confirmed against the schematic: pin high disables
    // the amp, pin low enables it.
    gpio_put(GPIO_RF_AMP_PWDN, !en);
}
