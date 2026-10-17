#ifndef TEST_GPIO_H
#define TEST_GPIO_H
#include <stdbool.h>
#include <stdint.h>
#define GPIO_OUT 1
#define GPIO_IN 0
#define GPIO_FUNC_I2C 3
#define GPIO_FUNC_PWM 4
enum gpio_drive_strength { GPIO_DRIVE_STRENGTH_12MA = 12 };
void gpio_init(unsigned int pin);
void gpio_set_dir(unsigned int pin, bool out);
void gpio_put(unsigned int pin, bool value);
bool gpio_get(unsigned int pin);
void gpio_pull_up(unsigned int pin);
void gpio_set_drive_strength(unsigned int pin, enum gpio_drive_strength drive);
void gpio_set_function(unsigned int pin, unsigned int function);
#endif
