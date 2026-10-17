#include "rf_task.h"

#include <math.h>
#include <stdatomic.h>

#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "pico/util/queue.h"

#include "measurement.h"

#define REQUEST_QUEUE_LENGTH 8

static queue_t requests, replies;
static atomic_uint cancel_seq;   // incremented by core 0 (rf_task_cancel())
static atomic_bool ready;        // core 1 has initialized the RF hardware
static atomic_uint busy_us;      // core 1's time spent on finished steps, wraps (rf_task_busy_us())
static atomic_uint step_start;   // time_us_64() at the start of the running step, low 32 bits
static atomic_bool stepping;     // a step is running
static atomic_uint core1_loops;  // core 1's main-loop passes (rf_task_core1_loops())
static uint8_t init_errors;      // written by core 1 before `ready`
static uint32_t next_tag;        // core 0

// --- Core 1 ---

static uint32_t active_seq; // cancel_seq of the request (or sweep) being executed

static struct {
    bool active;
    rf_sweep_config_t config;
    uint32_t tag, seq;
    uint32_t next; // index of the next point
} sweep;

// rf_app_hooks_t.cancelled: rf_task_cancel() was called after the running request was sent.
static bool cancelled(void) {
    return atomic_load(&cancel_seq) != active_seq;
}

static rf_status_t status_now(void) {
    rf_status_t s = {
        .mode = rf_app_get_mode(),
        .z = rf_app_get_z(),
        .port = rf_app_get_port(),
        .atten = rf_app_get_atten(),
        .source_on = rf_app_source_on(),
        .gen_on = rf_app_generator_on(),
        .pwr_on = rf_app_power_running(),
    };
    if (s.pwr_on) s.pwr_port = rf_app_power_port();
    if (s.gen_on) s.gen_freq = rf_app_generator_freq();
    return s;
}

// Final replies always reach core 0: it drains the queue in its main loop.
static void send_final(rf_reply_t *r) {
    r->final = true;
    r->status = status_now();
    queue_add_blocking(&replies, r);
}

// The grid of PROTOCOL.md `sweep`, computed exactly like the client's
// placeholder frequencies: linear in integers (fits: 6e9 * 4095 < 2^64),
// logarithmic rounded to the nearest Hz, the endpoints exact.
static uint64_t point_frequency(const rf_sweep_config_t *c, uint32_t i) {
    if (c->frequencies) return c->frequencies[i];
    if (i == 0) return c->f0;
    if (i == c->points - 1) return c->f1;
    if (c->logarithmic) {
        double log_f0 = log((double) c->f0), log_span = log((double) c->f1) - log_f0;
        return (uint64_t) llround(exp(log_f0 + log_span * (double) i / (double) (c->points - 1)));
    }
    return c->f0 + (c->f1 - c->f0) * (uint64_t) i / (uint64_t) (c->points - 1);
}

static void finish_sweep(rf_sweep_end_t end, uint32_t index, uint64_t freq_hz) {
    rf_app_sweep_end();
    sweep.active = false;
    rf_reply_t r = { .tag = sweep.tag, .kind = RF_REQ_SWEEP };
    r.result = end == RF_SWEEP_DONE ? RF_APP_OK : RF_APP_ERR_BUSY;
    r.data.sweep.end = end;
    r.data.sweep.index = index;
    r.data.sweep.freq_hz = freq_hz;
    send_final(&r);
}

static void start_sweep(const rf_req_t *req) {
    const rf_sweep_config_t *c = &req->arg.sweep;
    if (rf_app_get_mode() != RF_APP_SWEEP) {
        rf_reply_t r = { .tag = req->tag, .kind = RF_REQ_SWEEP, .result = RF_APP_ERR_WRONG_MODE };
        send_final(&r);
        return;
    }
    sweep.active = true;
    sweep.config = *c;
    sweep.tag = req->tag;
    sweep.seq = req->cancel_seq;
    sweep.next = 0;
    uint64_t first = c->points ? point_frequency(c, 0) : c->f0;
    if (cancelled()) { finish_sweep(RF_SWEEP_ABORTED, 0, first); return; }
    // An empty sweep or a session that cannot start ends right away.
    if (c->points == 0 || !rf_app_sweep_begin(c->z, first, c->allow_unlocked, c->atten))
        finish_sweep(cancelled() ? RF_SWEEP_ABORTED : RF_SWEEP_STARTUP_FAILED, 0, first);
}

static void sweep_step(void) {
    active_seq = sweep.seq;
    const rf_sweep_config_t *c = &sweep.config;
    uint32_t i = sweep.next;
    uint64_t f = point_frequency(c, i);
    if (cancelled()) { finish_sweep(RF_SWEEP_ABORTED, i, f); return; }

    uint8_t sparams = c->point_sparams ? c->point_sparams[i] : c->sparams;
    rf_reply_t r = { .tag = sweep.tag, .kind = RF_REQ_SWEEP };
    r.data.point.index = i;
    r.data.point.freq_hz = f;
    r.data.point.raw = c->path_mask ? rf_app_sweep_paths(f, c->port, c->path_mask)
                                    : rf_app_sweep_point(f, sparams & MEASUREMENT_S11, sparams & MEASUREMENT_S21,
                                                         sparams & MEASUREMENT_S12, sparams & MEASUREMENT_S22);
    if (cancelled()) { finish_sweep(RF_SWEEP_ABORTED, i, f); return; }
    if (!r.data.point.raw.measured && !c->continue_on_failure) {
        finish_sweep(RF_SWEEP_POINT_FAILED, i, f);
        return;
    }
    // Backpressure: wait while core 0 is behind, but never past a cancellation.
    while (!queue_try_add(&replies, &r)) {
        if (cancelled()) { finish_sweep(RF_SWEEP_ABORTED, i, f); return; }
        tight_loop_contents();
    }
    if (++sweep.next == c->points) finish_sweep(RF_SWEEP_DONE, i, f);
}

static rf_app_result_t manual(rf_manual_block_t block, uint64_t value) {
    // Never narrow an out-of-range value into a valid one (257 -> port 1).
    if (block != RF_MANUAL_SRC_FREQ && value > UINT8_MAX) return RF_APP_ERR_BAD_ARGS;
    switch (block) {
        case RF_MANUAL_LPF: return rf_app_manual_lpf((uint8_t) value);
        case RF_MANUAL_COUPLER: return rf_app_manual_coupler((uint8_t) value);
        case RF_MANUAL_PORT: return rf_app_manual_port((uint8_t) value);
        case RF_MANUAL_SRC_FREQ: return rf_app_manual_freq(value);
        case RF_MANUAL_SRC_EN: return rf_app_manual_source(value != 0);
        default: break;
    }
    return RF_APP_ERR_BAD_ARGS;
}

static void handle(const rf_req_t *req) {
    active_seq = req->cancel_seq;
    rf_reply_t r = { .tag = req->tag, .kind = req->kind, .result = RF_APP_OK };

    if (sweep.active) {
        // Between two points: only what cannot disturb the running sweep.
        switch (req->kind) {
            case RF_REQ_SET:
            case RF_REQ_STATUS:
                break;
            case RF_REQ_MODE_EXIT:
                finish_sweep(RF_SWEEP_ABORTED, sweep.next, point_frequency(&sweep.config, sweep.next));
                break;
            default:
                r.result = RF_APP_ERR_BUSY;
                send_final(&r);
                return;
        }
    }

    switch (req->kind) {
        case RF_REQ_MODE_ENTER: r.result = rf_app_mode_enter(req->arg.mode); break;
        case RF_REQ_MODE_EXIT: r.result = rf_app_mode_exit(); break;
        case RF_REQ_SET:
            if (req->arg.set.has_z) r.result = rf_app_set_z(req->arg.set.z);
            if (r.result == RF_APP_OK && req->arg.set.has_atten) r.result = rf_app_set_atten(req->arg.set.atten);
            break;
        case RF_REQ_STATUS: break;
        case RF_REQ_SWEEP: start_sweep(req); return; // replies itself
        case RF_REQ_GEN_START:
            r.result = rf_app_generator_start(req->arg.gen.port, req->arg.gen.z, req->arg.gen.atten,
                                              req->arg.gen.freq_hz);
            break;
        case RF_REQ_GEN_STOP: r.result = rf_app_generator_stop(); break;
        case RF_REQ_POWER_READ: r.result = rf_app_power_read(req->arg.power.port, req->arg.power.z, &r.data.raw); break;
        case RF_REQ_POWER_START: r.result = rf_app_power_start(req->arg.power.port, req->arg.power.z); break;
        case RF_REQ_POWER_SAMPLE: r.result = rf_app_power_sample(&r.data.raw); break;
        case RF_REQ_POWER_STOP: r.result = rf_app_power_stop(); break;
        case RF_REQ_PROBE:
            for (int path = 0; path < RF_APP_PATH_COUNT && r.result == RF_APP_OK; path++) {
                if (req->arg.path_mask & (1u << path))
                    r.result = rf_app_probe_path((rf_app_path_t) path, &r.data.probe[path]);
            }
            break;
        case RF_REQ_MANUAL: r.result = manual(req->arg.manual.block, req->arg.manual.value); break;
        case RF_REQ_TEMP: r.result = rf_app_read_temp_c(&r.data.temp_c); break;
        default: r.result = RF_APP_ERR_BAD_ARGS; break;
    }
    send_final(&r);
}

void rf_task_core1_init(void) {
    static const rf_app_hooks_t hooks = { .cancelled = cancelled };
    active_seq = atomic_load(&cancel_seq);
    init_errors = rf_app_init(&hooks);
    atomic_store(&ready, true);
}

// Only steps with work are timed: marking idle polls as running made
// rf_task_busy_us() jitter by a microsecond, and a reading below the previous
// one wrapped into a full-load window (BUSY stuck on while idle).
static uint32_t begin_step(void) {
    uint32_t started = (uint32_t) time_us_64();
    atomic_store(&step_start, started);
    atomic_store(&stepping, true);
    return started;
}

static void end_step(uint32_t started) {
    atomic_store(&stepping, false);
    atomic_fetch_add(&busy_us, (uint32_t) time_us_64() - started);
}

bool rf_task_step(void) {
    rf_req_t req;
    if (queue_try_remove(&requests, &req)) {
        uint32_t started = begin_step();
        handle(&req);
        end_step(started);
        return true;
    }
    if (sweep.active) {
        uint32_t started = begin_step();
        sweep_step();
        end_step(started);
        return true;
    }
    return false;
}

static void core1_main(void) {
    rf_task_core1_init();
    while (true) {
        if (!rf_task_step()) tight_loop_contents();
        atomic_fetch_add(&core1_loops, 1u);
    }
}

// --- Core 0 ---

void rf_task_start(void) {
    queue_init(&requests, sizeof(rf_req_t), REQUEST_QUEUE_LENGTH);
    queue_init(&replies, sizeof(rf_reply_t), RF_TASK_REPLY_QUEUE_LENGTH);
    multicore_launch_core1(core1_main);
}

uint32_t rf_task_core1_loops(void) {
    return atomic_load(&core1_loops);
}

uint8_t rf_task_wait_ready(void) {
    while (!atomic_load(&ready)) tight_loop_contents();
    return init_errors;
}

uint32_t rf_task_send(rf_req_t *req) {
    if (++next_tag == 0) ++next_tag; // 0 means "not sent"
    req->tag = next_tag;
    req->cancel_seq = atomic_load(&cancel_seq);
    return queue_try_add(&requests, req) ? req->tag : 0;
}

bool rf_task_poll(rf_reply_t *reply) {
    return queue_try_remove(&replies, reply);
}

// Includes the running step, so a long one (a lock wait) shows while it lasts. A
// read racing a step's end or start can be a few microseconds off either way, so
// it never returns less than the previous call did: callers take differences.
uint32_t rf_task_busy_us(void) {
    static uint32_t last;
    uint32_t busy = atomic_load(&busy_us);
    if (atomic_load(&stepping)) {
        uint32_t start = atomic_load(&step_start); // before the time: never later than now
        int32_t running = (int32_t) ((uint32_t) time_us_64() - start);
        if (running > 0) busy += (uint32_t) running;
    }
    if ((int32_t) (busy - last) > 0) last = busy;
    return last;
}

void rf_task_cancel(void) {
    atomic_fetch_add(&cancel_seq, 1u);
}
