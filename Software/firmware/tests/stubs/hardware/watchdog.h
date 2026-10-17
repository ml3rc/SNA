#ifndef TEST_WATCHDOG_H
#define TEST_WATCHDOG_H
#include <stdbool.h>
#include <stdint.h>
void watchdog_enable(uint32_t delay_ms, bool pause_on_debug);
void watchdog_update(void);
#endif
