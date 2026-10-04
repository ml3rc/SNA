#include "acquisition.h"
#include <math.h>
#include <stdatomic.h>
#include <assert.h>
#include "pico/multicore.h"
#include "pico/util/queue.h"
#include "pico/stdlib.h"
#include "tusb.h"
#include "main.h"
#include "protocol.h"

typedef struct { acquisition_config_t config; uint32_t session; } job_t;
static queue_t jobs, results;
static atomic_bool cancelled;
static bool active; // core 0 only
static uint32_t session;
static acquisition_stats_t last_stats; // core 0 only

void acquisition_cancel(void) { atomic_store(&cancelled, true); }
bool acquisition_cancelled(void) { return atomic_load(&cancelled); }

static bool publish(acquisition_result_t *r, acquisition_stats_t *stats) {
    uint64_t started = time_us_64();
    while (!queue_try_add(&results, r)) {
        if (acquisition_cancelled()) {
            stats->queue_wait_us += time_us_64() - started;
            return false;
        }
        sleep_us(50);
    }
    stats->queue_wait_us += time_us_64() - started;
    return true;
}

static void worker(void) {
    for (;;) {
        job_t job;
        queue_remove_blocking(&jobs, &job);
        acquisition_config_t c = job.config;
        acquisition_stats_t stats = {0};
        uint8_t previous_atten = rf_get_atten();
        if (c.override_atten) rf_set_atten(c.atten);
        rf_set_lock_timeout_us(c.timeout_us);
        rf_set_allow_unlocked(c.allow_unlocked);
        acquisition_result_t r = {.session=job.session, .kind=ACQ_START};
        uint64_t started = time_us_64();
        r.ok = !acquisition_cancelled() && rf_measure_session_start(c.z, c.f0);
        r.elapsed_us = time_us_64() - started;
        bool ready = r.ok;
        if (publish(&r, &stats) && ready) {
            for (uint32_t i=0; i<c.points && !acquisition_cancelled(); i++) {
                double frac = c.points > 1 ? (double)i/(c.points-1) : 0;
                uint64_t f = c.frequencies ? c.frequencies[i] : i == 0 ? c.f0 : i == c.points-1 ? c.f1 :
                    c.logarithmic ? (uint64_t)exp(log((double)c.f0) + frac*(log((double)c.f1)-log((double)c.f0))) :
                    (uint64_t)((double)c.f0 + frac*(double)(c.f1-c.f0));
                r = (acquisition_result_t){.session=job.session, .kind=ACQ_POINT, .point=i, .frequency=f};
                uint8_t mask = c.masks ? c.masks[i] :
                    (c.s11 ? 1 : 0) | (c.s21 ? 2 : 0) | (c.s12 ? 4 : 0) | (c.s22 ? 8 : 0);
                r.raw = rf_acquire_point(f, c.z, mask&1, mask&2, mask&4, mask&8);
                r.timing = rf_get_point_timing();
                stats.acquired++;
                stats.hardware_us += r.timing.total_us;
                r.ok = r.raw.valid;
                if (acquisition_cancelled() || !publish(&r, &stats) || (!r.raw.measured && !c.profiling)) break;
            }
        }
        started = time_us_64();
        rf_measure_session_end();
        if (c.override_atten) rf_set_atten(previous_atten);
        rf_set_lock_timeout_us(20000u);
        rf_set_allow_unlocked(false);
        r = (acquisition_result_t){.session=job.session, .kind=ACQ_DONE,
            .elapsed_us=time_us_64()-started, .stats=stats};
        // Core 0 drains while ending; DONE cannot be dropped even on cancellation.
        queue_add_blocking(&results, &r);
    }
}

void acquisition_init(void) {
    queue_init(&jobs, sizeof(job_t), 1);
    queue_init(&results, sizeof(acquisition_result_t), 4);
    multicore_launch_core1(worker);
}

static void service(void) {
    tud_task();
    poll_usb_commands();
    if (protocol_measurement_cancelled()) acquisition_cancel();
}

static acquisition_result_t receive(void) {
    acquisition_result_t r;
    for (;;) {
        service();
        if (queue_try_remove(&results, &r)) {
            if (r.session == session) return r;
        } else sleep_us(50);
    }
}

bool acquisition_begin(acquisition_config_t config, uint64_t *startup_us) {
    assert(!active);
    active = true;
    atomic_store(&cancelled, false);
    job_t job = {.config=config, .session=++session};
    queue_add_blocking(&jobs, &job);
    acquisition_result_t r = receive();
    assert(r.kind == ACQ_START);
    *startup_us = r.elapsed_us;
    return r.ok;
}

bool acquisition_next(acquisition_result_t *r) {
    *r = receive();
    // Every configured point yields a result unless core 0 has cancelled.
    // Preserve DONE for end() if cancellation was observed while receiving.
    if (r->kind == ACQ_DONE) {
        queue_add_blocking(&results, r);
        return false;
    }
    return r->kind == ACQ_POINT;
}

uint64_t acquisition_end(void) {
    assert(active);
    acquisition_cancel();
    acquisition_result_t r;
    do { r = receive(); } while (r.kind != ACQ_DONE);
    active = false;
    last_stats = r.stats;
    return r.elapsed_us;
}

acquisition_stats_t acquisition_stats(void) { return last_stats; }
