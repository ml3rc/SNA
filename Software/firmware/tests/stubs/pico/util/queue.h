#ifndef TEST_QUEUE_H
#define TEST_QUEUE_H
#include <windows.h>
#include <stdbool.h>
typedef struct {
    CRITICAL_SECTION lock;
    unsigned char *data;
    unsigned int size, capacity, read, write, count;
} queue_t;
void queue_init(queue_t *q, unsigned int size, unsigned int capacity);
bool queue_try_add(queue_t *q, const void *value);
bool queue_try_remove(queue_t *q, void *value);
void queue_add_blocking(queue_t *q, const void *value);
void queue_remove_blocking(queue_t *q, void *value);
#endif
