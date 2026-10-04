#include "hal/ad8318.h"

#include "hardware/adc.h"
#include "hardware/gpio.h"

#include "sna.h"

void ad8318_init(void) {
    gpio_init(GPIO_DET_EN);
    gpio_set_dir(GPIO_DET_EN, GPIO_OUT);

    // adc_init() is global RP2350 ADC peripheral setup, not AD8318-specific,
    // but this is currently the only ADC consumer on the board -- revisit
    // (move it to a shared board-level init) if that changes.
    adc_init();
    adc_gpio_init(GPIO_DET_OUT_ADC);
}

void ad8318_enable(bool en) {
    gpio_put(GPIO_DET_EN, en);
}

uint16_t ad8318_read_raw(void) {
    // GPIO_DET_OUT_ADC (GPIO45, pkg pin 56) -> ADC channel 5, confirmed
    // against the RP2350 datasheet's QFN-80 pinout figure -- see
    // boards/sna.h and Software/protocol/PINMAP.md.
    adc_select_input(ADC_CHANNEL_DET);
    return adc_read();
}
