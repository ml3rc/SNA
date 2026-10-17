// Runs the real core-1 loop (rf_task.c) on a second Windows thread against a
// stub application layer: ordering, sweeps, backpressure, cancellation and
// what core 1 accepts between sweep points.
#include <assert.h>
#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "pico/util/queue.h"
#include "rf_task.h"

static DWORD core1_thread_id;
unsigned int get_core_num(void) { return GetCurrentThreadId() == core1_thread_id ? 1 : 0; }
static void (*core1_entry)(void);
static DWORD WINAPI core1_thread(void *unused) {
    (void)unused; core1_thread_id = GetCurrentThreadId(); core1_entry(); return 0;
}
void multicore_launch_core1(void (*entry)(void)) {
    core1_entry = entry;
    HANDLE thread = CreateThread(NULL, 0, core1_thread, NULL, 0, NULL);
    assert(thread); CloseHandle(thread);
}
static uint64_t deadline;
uint64_t time_us_64(void) { return (uint64_t)GetTickCount64() * 1000; }
void sleep_us(uint64_t us) { (void)us; assert(time_us_64() < deadline); Sleep(0); }

// --- stub application layer: every call must come from core 1 ---
#define CORE1() assert(get_core_num() == 1)
static rf_app_hooks_t hooks;
static rf_app_mode_t mode = RF_APP_IDLE;
static uint8_t z = 50, atten = 127;
static atomic_bool session;
static atomic_uint acquired;
static bool startup_ok = true, wait_in_point;
static int failure_point = -1;
static uint64_t last_f;
uint8_t rf_app_init(const rf_app_hooks_t *h) { CORE1(); hooks = *h; return RF_APP_INIT_ERR_DETECTOR; }
rf_app_mode_t rf_app_get_mode(void) { CORE1(); return mode; }
uint8_t rf_app_get_z(void) { CORE1(); return z; }
uint8_t rf_app_get_port(void) { CORE1(); return 1; }
uint8_t rf_app_get_atten(void) { CORE1(); return atten; }
bool rf_app_source_on(void) { CORE1(); return atomic_load(&session); }
bool rf_app_generator_on(void) { CORE1(); return false; }
uint64_t rf_app_generator_freq(void) { CORE1(); return 0; }
bool rf_app_power_running(void) { CORE1(); return false; }
uint8_t rf_app_power_port(void) { CORE1(); return 1; }
rf_app_result_t rf_app_mode_enter(rf_app_mode_t m) {
    CORE1(); if (mode != RF_APP_IDLE) return RF_APP_ERR_MODE_ACTIVE; mode = m; return RF_APP_OK;
}
rf_app_result_t rf_app_mode_exit(void) {
    CORE1(); if (atomic_load(&session)) return RF_APP_ERR_BUSY; mode = RF_APP_IDLE; return RF_APP_OK;
}
rf_app_result_t rf_app_set_z(uint8_t value) { CORE1(); z = value; return RF_APP_OK; }
rf_app_result_t rf_app_set_atten(uint8_t value) { CORE1(); atten = value; return RF_APP_OK; }
bool rf_app_sweep_begin(uint8_t zz, uint64_t f, bool allow_unlocked, uint8_t a) {
    CORE1(); (void)allow_unlocked;
    assert(mode == RF_APP_SWEEP && !atomic_load(&session) && f >= 50000000ULL);
    z = zz; atten = a; atomic_store(&acquired, 0);
    atomic_store(&session, true);
    return startup_ok;
}
void rf_app_sweep_end(void) { CORE1(); atomic_store(&session, false); }
rf_app_raw_measurement_t rf_app_sweep_point(uint64_t f, bool a, bool b, bool c, bool d) {
    CORE1(); assert(atomic_load(&session) && (a || b || c || d));
    unsigned index = atomic_fetch_add(&acquired, 1);
    last_f = f;
    while (wait_in_point && !hooks.cancelled()) Sleep(0); // a lock wait
    bool ok = (int)index != failure_point;
    return (rf_app_raw_measurement_t){.valid = ok, .measured = ok, .source1_ref = atten,
                                      .source1_reflection = (uint16_t)(1000 + index)};
}
rf_app_raw_measurement_t rf_app_sweep_paths(uint64_t f, uint8_t port, uint8_t mask) {
    CORE1(); assert(atomic_load(&session) && port == 2 && mask == 0x9);
    (void)f; atomic_fetch_add(&acquired, 1);
    return (rf_app_raw_measurement_t){.valid = true, .measured = true, .path_raw = {7, 0, 0, 9}};
}
rf_app_result_t rf_app_generator_start(uint8_t p, uint8_t zz, uint8_t a, uint64_t f) {
    CORE1(); (void)p; (void)zz; (void)a; (void)f; return RF_APP_ERR_WRONG_MODE;
}
rf_app_result_t rf_app_generator_stop(void) { CORE1(); return RF_APP_ERR_WRONG_MODE; }
rf_app_result_t rf_app_power_read(uint8_t p, uint8_t zz, uint16_t *raw) { CORE1(); (void)p; (void)zz; *raw = 1; return RF_APP_OK; }
rf_app_result_t rf_app_power_start(uint8_t p, uint8_t zz) { CORE1(); (void)p; (void)zz; return RF_APP_OK; }
rf_app_result_t rf_app_power_sample(uint16_t *raw) { CORE1(); *raw = 1; return RF_APP_OK; }
rf_app_result_t rf_app_power_stop(void) { CORE1(); return RF_APP_OK; }
rf_app_result_t rf_app_manual_lpf(uint8_t i) { CORE1(); (void)i; return RF_APP_OK; }
rf_app_result_t rf_app_manual_coupler(uint8_t p) { CORE1(); (void)p; return RF_APP_OK; }
rf_app_result_t rf_app_manual_freq(uint64_t f) { CORE1(); (void)f; return RF_APP_OK; }
rf_app_result_t rf_app_manual_port(uint8_t p) { CORE1(); (void)p; return RF_APP_OK; }
rf_app_result_t rf_app_manual_source(bool on) { CORE1(); (void)on; return RF_APP_OK; }
rf_app_result_t rf_app_probe_path(rf_app_path_t p, rf_app_detector_sample_t *out) {
    CORE1(); *out = (rf_app_detector_sample_t){(uint16_t)(100 + p), 0, 0}; return RF_APP_OK;
}
rf_app_result_t rf_app_read_temp_c(double *t) { CORE1(); *t = 30.0; return RF_APP_OK; }

// --- core 0 side ---
static rf_reply_t next_reply(void) {
    rf_reply_t r;
    while (!rf_task_poll(&r)) { assert(time_us_64() < deadline); Sleep(0); }
    return r;
}
static uint32_t send_req(rf_req_t req) {
    uint32_t tag = rf_task_send(&req); assert(tag); return tag;
}
static rf_reply_t call(rf_req_t req) {
    uint32_t tag = send_req(req);
    rf_reply_t r = next_reply();
    assert(r.tag == tag && r.final && r.kind == req.kind);
    return r;
}
static rf_req_t sweep_req(uint32_t points) {
    return (rf_req_t){.kind = RF_REQ_SWEEP, .arg.sweep = {
        .f0 = 50000000ULL, .f1 = 6000000000ULL, .points = points, .z = 75, .atten = 3, .sparams = 1,
    }};
}
// The rest of a sweep: its points, then its final reply.
static rf_reply_t drain_sweep(uint32_t tag, unsigned *points) {
    for (;;) {
        rf_reply_t r = next_reply();
        assert(r.tag == tag);
        if (r.final) return r;
        ++*points;
    }
}

int main(void) {
    deadline = time_us_64() + 20000000;
    rf_task_start();
    assert(rf_task_wait_ready() == RF_APP_INIT_ERR_DETECTOR);

    // Replies come back in order, each with the state after its request.
    uint32_t t1 = send_req((rf_req_t){.kind = RF_REQ_STATUS});
    uint32_t t2 = send_req((rf_req_t){.kind = RF_REQ_MODE_ENTER, .arg.mode = RF_APP_SWEEP});
    uint32_t t3 = send_req((rf_req_t){.kind = RF_REQ_MODE_ENTER, .arg.mode = RF_APP_MANUAL});
    rf_reply_t r = next_reply(); assert(r.tag == t1 && r.status.mode == RF_APP_IDLE);
    r = next_reply(); assert(r.tag == t2 && r.result == RF_APP_OK && r.status.mode == RF_APP_SWEEP);
    r = next_reply(); assert(r.tag == t3 && r.result == RF_APP_ERR_MODE_ACTIVE && r.status.mode == RF_APP_SWEEP);
    puts("PASS: core 1 initializes the RF side, replies in order with the resulting state");

    // Linear and logarithmic grids, exact endpoints, every point in order.
    for (int run = 0; run < 2; run++) {
        rf_req_t req = sweep_req(201); req.arg.sweep.logarithmic = run != 0;
        uint32_t tag = send_req(req);
        for (uint32_t i = 0; i < 201; i++) {
            r = next_reply();
            assert(r.tag == tag && !r.final && r.data.point.index == i && r.data.point.raw.valid);
            assert(r.data.point.raw.source1_reflection == 1000 + i);
            // PROTOCOL.md: exact integers when linear, rounded to the Hz when logarithmic.
            const rf_sweep_config_t *c = &req.arg.sweep;
            uint64_t expected = i == 0 ? c->f0 : i == 200 ? c->f1 : c->logarithmic ?
                (uint64_t)llround(exp(log((double)c->f0) + (log((double)c->f1) - log((double)c->f0)) * i / 200.0)) :
                c->f0 + (c->f1 - c->f0) * i / 200;
            assert(r.data.point.freq_hz == expected);
        }
        r = next_reply();
        assert(r.tag == tag && r.final && r.result == RF_APP_OK && r.data.sweep.end == RF_SWEEP_DONE);
        assert(!r.status.source_on && r.status.z == 75 && r.status.atten == 3);
    }
    puts("PASS: linear/log grids with exact endpoints, points in order, final reply after the last point");

    // Backpressure: a core 0 that does not read stops core 1 after a few points.
    uint32_t tag = send_req(sweep_req(100));
    Sleep(50);
    assert(atomic_load(&acquired) <= 10 && atomic_load(&session));
    rf_task_cancel();
    unsigned points = 0;
    r = drain_sweep(tag, &points);
    assert(r.data.sweep.end == RF_SWEEP_ABORTED && points < 100 && !atomic_load(&session));
    // Cancelled while waiting for lock inside a point.
    wait_in_point = true;
    tag = send_req(sweep_req(3));
    while (atomic_load(&acquired) == 0) Sleep(0);
    rf_task_cancel();
    points = 0;
    r = drain_sweep(tag, &points);
    assert(r.data.sweep.end == RF_SWEEP_ABORTED && points == 0);
    wait_in_point = false;
    // A request sent after the cancellation is not affected by it.
    tag = send_req(sweep_req(2)); points = 0;
    assert(drain_sweep(tag, &points).data.sweep.end == RF_SWEEP_DONE && points == 2);
    puts("PASS: bounded backpressure, cancellation between points and inside a lock wait, later requests unaffected");

    // Startup and point failures; continue_on_failure measures everything.
    startup_ok = false;
    tag = send_req(sweep_req(3)); points = 0;
    r = drain_sweep(tag, &points);
    assert(r.data.sweep.end == RF_SWEEP_STARTUP_FAILED && points == 0 && !atomic_load(&session));
    startup_ok = true;
    failure_point = 1;
    tag = send_req(sweep_req(3)); points = 0;
    r = drain_sweep(tag, &points);
    assert(r.data.sweep.end == RF_SWEEP_POINT_FAILED && r.data.sweep.index == 1 && points == 1);
    rf_req_t lenient = sweep_req(3); lenient.arg.sweep.continue_on_failure = true;
    tag = send_req(lenient); points = 0;
    r = drain_sweep(tag, &points);
    assert(r.data.sweep.end == RF_SWEEP_DONE && points == 3);
    failure_point = -1;
    // Explicit grid and per-point S-parameters (the SDI test).
    uint64_t frequencies[] = {6000000000ULL, 50000000ULL, 1485000000ULL};
    uint8_t sparams[] = {1, 8, 2};
    rf_req_t sdi = sweep_req(3);
    sdi.arg.sweep.frequencies = frequencies; sdi.arg.sweep.point_sparams = sparams;
    tag = send_req(sdi);
    for (int i = 0; i < 3; i++) { r = next_reply(); assert(!r.final && r.data.point.freq_hz == frequencies[i]); }
    assert(next_reply().final);
    // Raw detector sweep on port 2.
    rf_req_t raw = sweep_req(1); raw.arg.sweep.sparams = 0; raw.arg.sweep.path_mask = 0x9; raw.arg.sweep.port = 2;
    tag = send_req(raw);
    r = next_reply(); assert(!r.final && r.data.point.raw.path_raw[0] == 7 && r.data.point.raw.path_raw[3] == 9);
    assert(next_reply().final);
    puts("PASS: startup/point failure, continue on failure, explicit grids, raw detector sweeps");

    // Between points: settings and status are served, other requests are busy,
    // mode exit ends the sweep first (its final reply comes before the exit's).
    wait_in_point = false;
    tag = send_req(sweep_req(4000));
    uint32_t set_tag = send_req((rf_req_t){.kind = RF_REQ_SET, .arg.set = {.has_atten = true, .atten = 40}});
    uint32_t busy_tag = send_req((rf_req_t){.kind = RF_REQ_PROBE, .arg.path_mask = 1});
    bool set_seen = false, busy_seen = false, atten_applied = false;
    while (!(set_seen && busy_seen && atten_applied)) {
        r = next_reply();
        if (r.tag == set_tag) { set_seen = true; assert(r.result == RF_APP_OK && r.status.atten == 40); }
        else if (r.tag == busy_tag) { busy_seen = true; assert(r.result == RF_APP_ERR_BUSY); }
        else { assert(r.tag == tag && !r.final); if (set_seen && r.data.point.raw.source1_ref == 40) atten_applied = true; }
    }
    rf_task_cancel(); // what the protocol does on mode_exit
    uint32_t exit_tag = send_req((rf_req_t){.kind = RF_REQ_MODE_EXIT});
    points = 0;
    r = drain_sweep(tag, &points);
    assert(r.data.sweep.end == RF_SWEEP_ABORTED);
    r = next_reply();
    assert(r.tag == exit_tag && r.result == RF_APP_OK && r.status.mode == RF_APP_IDLE);
    // A sweep outside the sweep mode is refused without a session.
    r = call(sweep_req(2));
    assert(r.result == RF_APP_ERR_WRONG_MODE && !atomic_load(&session));
    // Probe and temperature replies carry their data.
    r = call((rf_req_t){.kind = RF_REQ_PROBE, .arg.path_mask = 0xA});
    assert(r.result == RF_APP_OK && r.data.probe[1].median == 101 && r.data.probe[3].median == 103 && r.data.probe[0].median == 0);
    r = call((rf_req_t){.kind = RF_REQ_TEMP});
    assert(r.result == RF_APP_OK && r.data.temp_c == 30.0);
    puts("PASS: settings and status between sweep points, other requests busy, mode exit ends the sweep first");
    return 0;
}
