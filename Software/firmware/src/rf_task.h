#ifndef SNA_RF_TASK_H
#define SNA_RF_TASK_H

#include <stdbool.h>
#include <stdint.h>

#include "rf_control/rf_control.h"

// Core 1 owns the RF hardware: it runs rf_app_* (and so everything below it)
// and nothing else. Core 0 (USB, JSON, EEPROM, calibration) reaches it only
// through this module: requests go into one queue, replies come back in
// another, both first in first out. Every request gets exactly one final
// reply; a sweep sends its points before it. Between two sweep points core 1
// still takes requests: settings, status and mode exit (which ends the sweep
// first); everything else is refused with RF_APP_ERR_BUSY.
//
// rf_task_cancel() cancels every request sent before it, through
// rf_app_hooks_t: a sweep ends (RF_SWEEP_ABORTED) and lock waits give up.

typedef enum {
    RF_REQ_MODE_ENTER,   // arg.mode
    RF_REQ_MODE_EXIT,    // ends a running sweep first
    RF_REQ_SET,          // arg.set
    RF_REQ_STATUS,       // only the status every final reply carries
    RF_REQ_SWEEP,        // arg.sweep; points, then the final reply
    RF_REQ_GEN_START,    // arg.gen
    RF_REQ_GEN_STOP,
    RF_REQ_POWER_READ,   // arg.power -> data.raw
    RF_REQ_POWER_START,  // arg.power
    RF_REQ_POWER_SAMPLE, // -> data.raw
    RF_REQ_POWER_STOP,
    RF_REQ_PROBE,        // arg.path_mask -> data.probe
    RF_REQ_MANUAL,       // arg.manual
    RF_REQ_TEMP,         // -> data.temp_c
} rf_req_kind_t;

typedef enum {
    RF_MANUAL_LPF,      // value 0..RF_APP_LPF_COUNT-1
    RF_MANUAL_COUPLER,  // value rf_app_path_t
    RF_MANUAL_PORT,     // value 0 (terminate), 1, 2
    RF_MANUAL_SRC_FREQ, // value Hz
    RF_MANUAL_SRC_EN,   // value 0/1
    RF_MANUAL_COUNT,
} rf_manual_block_t;

typedef struct {
    uint64_t f0, f1;     // the grid: f0 + (f1-f0)*i/(points-1), or geometric when logarithmic
    uint32_t points;
    bool logarithmic;
    uint8_t z, atten;    // applied before the first point, they stay the settings afterwards
    uint8_t sparams;     // bit 0..3 = s11, s21, s12, s22 (MEASUREMENT_S*)
    uint8_t path_mask;   // raw detector sweep instead: rf_app_path_t bits, source on `port`
    uint8_t port;
    bool allow_unlocked;
    bool continue_on_failure;  // measure every point even if one fails (SDI test)
    // Optional explicit grid and per-point sparams (SDI test), `points` long;
    // core 0 keeps them unchanged until the sweep's final reply.
    const uint64_t *frequencies;
    const uint8_t *point_sparams;
} rf_sweep_config_t;

typedef struct {
    rf_req_kind_t kind;
    uint32_t tag;        // set by rf_task_send(), echoed in every reply
    uint32_t cancel_seq; // set by rf_task_send()
    union {
        rf_app_mode_t mode;
        struct { bool has_z, has_atten; uint8_t z, atten; } set;
        rf_sweep_config_t sweep;
        struct { uint8_t port, z, atten; uint64_t freq_hz; } gen;
        struct { uint8_t port, z; } power;
        uint8_t path_mask;
        struct { rf_manual_block_t block; uint64_t value; } manual;
    } arg;
} rf_req_t;

// The RF state after a request, in every final reply.
typedef struct {
    rf_app_mode_t mode;
    uint8_t z, port, atten;
    bool source_on, gen_on, pwr_on;
    uint8_t pwr_port;  // while pwr_on
    uint64_t gen_freq; // while gen_on
} rf_status_t;

typedef enum {
    RF_SWEEP_DONE,           // every point measured
    RF_SWEEP_ABORTED,        // rf_task_cancel() or mode exit
    RF_SWEEP_STARTUP_FAILED, // no initial lock (strict) or tune failure, before the first point
    RF_SWEEP_POINT_FAILED,   // a point could not be measured (strict lock, tune failure)
} rf_sweep_end_t;

typedef struct {
    uint32_t tag;
    rf_req_kind_t kind;     // of the request answered
    bool final;             // false: a sweep point, more replies follow
    rf_app_result_t result; // final replies
    rf_status_t status;     // final replies
    union {
        struct { uint32_t index; uint64_t freq_hz; rf_app_raw_measurement_t raw; } point;
        struct { rf_sweep_end_t end; uint32_t index; uint64_t freq_hz; } sweep; // where it ended
        uint16_t raw;
        rf_app_detector_sample_t probe[RF_APP_PATH_COUNT];
        double temp_c;
    } data;
} rf_reply_t;

// Replies core 1 can queue before it waits for core 0.
#define RF_TASK_REPLY_QUEUE_LENGTH 8

// --- Core 0 ---
// Launches core 1, which initializes the RF hardware and then serves requests.
void rf_task_start(void);
// Waits until core 1 has initialized the RF hardware; its rf_app_init_error_t bits.
uint8_t rf_task_wait_ready(void);
// Queues `req` (filling in tag and cancel_seq). Returns the tag, 0 if the queue is full.
uint32_t rf_task_send(rf_req_t *req);
// The next reply, false if there is none yet.
bool rf_task_poll(rf_reply_t *reply);
// Cancels every request sent so far: a running sweep ends, lock waits give up.
void rf_task_cancel(void);
// Microseconds core 1 has spent handling requests and sweep points (not idling), the
// running one included; a total that wraps, the difference of two calls is the work in between.
uint32_t rf_task_busy_us(void);
// Counts core 1's main-loop passes (one per handled step, or idle spin); a value that
// stops changing means core 1 is stuck in one step. Feeds the watchdog (main.c).
uint32_t rf_task_core1_loops(void);

// --- Core 1 pieces, public for the host tests (which run both cores on one thread) ---
void rf_task_core1_init(void);
// Handles one request or measures one sweep point; false if there was nothing to do.
bool rf_task_step(void);

#endif
