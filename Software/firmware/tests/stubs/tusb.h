#ifndef TEST_TUSB_H
#define TEST_TUSB_H
#include <stdbool.h>
#include <stdint.h>
void tusb_init(void);
void tud_task(void);
bool tud_disconnect(void);
bool tud_connect(void);
bool tud_cdc_connected(void);
uint32_t tud_cdc_available(void);
uint32_t tud_cdc_read(void *buf, uint32_t count);
uint32_t tud_cdc_write(const void *buf, uint32_t count);
void tud_cdc_write_flush(void);
#endif
