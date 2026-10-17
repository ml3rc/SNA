#ifndef TEST_STDLIB_H
#define TEST_STDLIB_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
typedef unsigned int uint;
typedef uint64_t absolute_time_t;
#define count_of(a) (sizeof(a) / sizeof((a)[0]))
static inline void tight_loop_contents(void) {}
void sleep_us(uint64_t us);
void sleep_ms(uint32_t ms);
absolute_time_t get_absolute_time(void);
uint64_t time_us_64(void);
uint32_t to_ms_since_boot(absolute_time_t t);
typedef struct repeating_timer repeating_timer_t;
struct repeating_timer { void *user_data; };
typedef bool (*repeating_timer_callback_t)(repeating_timer_t *rt);
bool add_repeating_timer_ms(int32_t delay_ms, repeating_timer_callback_t callback, void *user_data, repeating_timer_t *out);
bool cancel_repeating_timer(repeating_timer_t *timer);
#endif
