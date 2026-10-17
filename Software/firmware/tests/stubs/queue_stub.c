// pico/util/queue.h for the host tests: a bounded FIFO behind a Windows
// critical section, so it also works between two real threads.
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "pico/util/queue.h"

void queue_init(queue_t *q, unsigned int size, unsigned int capacity) {
    InitializeCriticalSection(&q->lock); q->size=size; q->capacity=capacity;
    q->read=q->write=q->count=0;
    q->data=calloc(size,capacity); assert(q->data);
}
bool queue_try_add(queue_t *q, const void *value) {
    EnterCriticalSection(&q->lock); bool ok=q->count<q->capacity;
    if(ok) { memcpy(q->data+q->write*q->size,value,q->size); q->write=(q->write+1)%q->capacity; q->count++; }
    LeaveCriticalSection(&q->lock); return ok;
}
bool queue_try_remove(queue_t *q, void *value) {
    EnterCriticalSection(&q->lock); bool ok=q->count>0;
    if(ok) { memcpy(value,q->data+q->read*q->size,q->size); q->read=(q->read+1)%q->capacity; q->count--; }
    LeaveCriticalSection(&q->lock); return ok;
}
void queue_add_blocking(queue_t *q, const void *v) { while(!queue_try_add(q,v)) Sleep(0); }
void queue_remove_blocking(queue_t *q, void *v) { while(!queue_try_remove(q,v)) Sleep(0); }
