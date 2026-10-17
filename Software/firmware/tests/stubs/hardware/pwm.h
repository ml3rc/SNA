#ifndef TEST_PWM_H
#define TEST_PWM_H
#include <stdbool.h>
#include <stdint.h>
unsigned int pwm_gpio_to_slice_num(unsigned int gpio);
void pwm_set_wrap(unsigned int slice, uint16_t wrap);
void pwm_set_clkdiv(unsigned int slice, float divider);
void pwm_set_enabled(unsigned int slice, bool enabled);
void pwm_set_gpio_level(unsigned int gpio, uint16_t level);
#endif
