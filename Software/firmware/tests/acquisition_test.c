// Execute the real producer/consumer on two Windows threads; bounded queue stub.
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include "pico/util/queue.h"
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "acquisition.h"

static atomic_uint worker_id;
static atomic_bool rf_on, cancel_requested;
static atomic_uint acquired;
static bool startup_ok = true;
static int failure_point = -1;
static bool wait_in_lock;
static rf_point_timing_t timing;
static uint32_t timeout;
static bool fallback;
static atomic_uchar atten = 64;
static int expected_atten = -1;
unsigned int get_core_num(void);
uint8_t rf_get_atten(void) { return atomic_load(&atten); }
void rf_set_atten(uint8_t value) { assert(get_core_num()==1); atomic_store(&atten, value); }
void rf_set_allow_unlocked(bool allow) { assert(get_core_num()==1); fallback=allow; }
static uint64_t deadline;
static void (*entry_point)(void);
unsigned int get_core_num(void) { return GetCurrentThreadId() == worker_id ? 1 : 0; }
static DWORD WINAPI start_thread(void *unused) {
    (void)unused; worker_id=GetCurrentThreadId(); entry_point(); return 0;
}
void multicore_launch_core1(void (*entry)(void)) {
    entry_point=entry;
    HANDLE thread=CreateThread(NULL,0,start_thread,NULL,0,NULL);
    assert(thread); CloseHandle(thread);
}
void queue_init(queue_t *q, unsigned int size, unsigned int capacity) {
    InitializeCriticalSection(&q->lock); q->size=size; q->capacity=capacity;
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
void queue_add_blocking(queue_t *q, const void *v) { while(!queue_try_add(q,v)) Sleep(1); }
void queue_remove_blocking(queue_t *q, void *v) { while(!queue_try_remove(q,v)) Sleep(1); }
uint64_t time_us_64(void) { return (uint64_t)GetTickCount()*1000; }
void sleep_us(uint64_t us) { (void)us; assert(time_us_64()<deadline); Sleep(1); }
void tud_task(void) { assert(get_core_num()==0); }
void poll_usb_commands(void) { assert(get_core_num()==0); }
bool protocol_measurement_cancelled(void) { assert(get_core_num()==0); return atomic_load(&cancel_requested); }
void rf_set_lock_timeout_us(uint32_t value) { assert(get_core_num()==1); timeout=value; }
bool rf_measure_session_start(uint8_t z, uint64_t f) {
    assert(get_core_num()==1 && (z==50 || z==75) && f>=50000000);
    atomic_store(&acquired,0); atomic_store(&rf_on,true); return startup_ok;
}
void rf_measure_session_end(void) { assert(get_core_num()==1); atomic_store(&rf_on,false); }
rf_raw_measurement_t rf_acquire_point(uint64_t f, uint8_t z, bool a, bool b, bool c, bool d) {
    assert(get_core_num()==1 && atomic_load(&rf_on));
    (void)f; (void)z; assert(a || b || c || d);
    if (expected_atten >= 0) assert(rf_get_atten()==expected_atten);
    unsigned int index=atomic_fetch_add(&acquired,1);
    while(wait_in_lock && !acquisition_cancelled()) sleep_us(50);
    timing=(rf_point_timing_t){.tune_us=100,.adc_reads=6,.total_us=100};
    return (rf_raw_measurement_t){.valid=(int)index!=failure_point,.measured=(int)index!=failure_point || fallback,.source1_ref=(uint16_t)(1000+index)};
}
rf_point_timing_t rf_get_point_timing(void) { assert(get_core_num()==1); return timing; }

static acquisition_config_t config(unsigned int points) {
    return (acquisition_config_t){.f0=50000000,.f1=6000000000ULL,.points=points,.timeout_us=2000000,
        .z=50,.s11=true,.s21=true,.s12=true,.s22=true,.profiling=true};
}
static void finish(void) {
    acquisition_end(); assert(!atomic_load(&rf_on)); assert(timeout==20000);
    atomic_store(&cancel_requested,false);
}
int main(void) {
    deadline=time_us_64()+15000000; acquisition_init();
    uint64_t startup; acquisition_result_t r; uint32_t old_session=0;
    for(int run=0;run<2;run++) {
        acquisition_config_t c=config(201); c.logarithmic=run!=0;
        assert(acquisition_begin(c,&startup));
        for(unsigned int i=0;i<201;i++) {
            assert(acquisition_next(&r)); assert(r.point==i && r.raw.valid);
            assert(r.raw.source1_ref==1000+i && r.timing.adc_reads==6);
            assert(r.session>old_session);
            double fraction=(double)i/200;
            uint64_t expected=i==0?c.f0:i==200?c.f1:c.logarithmic?
                (uint64_t)exp(log((double)c.f0)+fraction*(log((double)c.f1)-log((double)c.f0))):
                (uint64_t)((double)c.f0+fraction*(double)(c.f1-c.f0));
            assert(r.frequency==expected);
        }
        old_session=r.session; finish();
    }
    assert(acquisition_begin(config(100),&startup));
    Sleep(30); // Deliberately let results fill; production must remain bounded.
    assert(atomic_load(&acquired)<=5); assert(atomic_load(&rf_on));
    atomic_store(&cancel_requested,true); finish();
    assert(acquisition_begin(config(1),&startup)); assert(acquisition_next(&r));
    assert(r.point==0 && r.session>old_session); finish();
    startup_ok=false; assert(!acquisition_begin(config(1),&startup)); finish(); startup_ok=true;
    failure_point=1;
    assert(acquisition_begin(config(3),&startup));
    for(unsigned int i=0;i<3;i++) { assert(acquisition_next(&r)); assert(r.raw.valid==(i!=1)); }
    finish();
    acquisition_config_t c=config(3); c.profiling=false;
    assert(acquisition_begin(c,&startup)); assert(acquisition_next(&r) && r.raw.valid);
    assert(acquisition_next(&r) && !r.raw.valid); assert(!acquisition_next(&r)); finish(); failure_point=-1;
    wait_in_lock=true; assert(acquisition_begin(config(3),&startup));
    atomic_store(&cancel_requested,true); assert(!acquisition_next(&r)); finish(); wait_in_lock=false;
    failure_point=1; c=config(3); c.profiling=false; c.allow_unlocked=true;
    assert(acquisition_begin(c,&startup));
    for(unsigned int i=0;i<3;i++) {
        assert(acquisition_next(&r)); assert(r.raw.measured && r.raw.valid==(i!=1));
    }
    finish(); assert(!fallback); failure_point=-1;
    uint64_t frequencies[]={6000000000ULL,50000000,1500000000};
    uint8_t masks[]={1,8,2}; c=config(3); c.f0=frequencies[0];
    c.frequencies=frequencies; c.masks=masks;
    assert(acquisition_begin(c,&startup));
    for(unsigned int i=0;i<3;i++) { assert(acquisition_next(&r)); assert(r.frequency==frequencies[i]); }
    finish();
    c=config(3); c.override_atten=true; c.atten=0; expected_atten=0;
    assert(acquisition_begin(c,&startup));
    for(unsigned int i=0;i<3;i++) assert(acquisition_next(&r));
    finish(); assert(rf_get_atten()==64);
    assert(acquisition_begin(c,&startup)); Sleep(30); atomic_store(&cancel_requested,true);
    finish(); assert(rf_get_atten()==64);
    startup_ok=false; assert(!acquisition_begin(c,&startup)); finish();
    assert(rf_get_atten()==64); startup_ok=true; expected_atten=-1;
    puts("PASS: concurrent acquisition, linear/log grids, bounded backpressure, cancellation, startup/point failure, shutdown and new sessions");
    return 0;
}


