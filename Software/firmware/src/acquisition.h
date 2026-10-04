#ifndef SNA_ACQUISITION_H
#define SNA_ACQUISITION_H
#include "rf_control.h"

typedef struct {
    uint64_t queue_wait_us, hardware_us;
    uint32_t acquired;
} acquisition_stats_t;

typedef struct {
    uint64_t f0, f1;
    uint32_t points, timeout_us;
    uint8_t z;
    bool s11, s21, s12, s22, logarithmic, profiling;
    bool allow_unlocked;
    bool override_atten;
    uint8_t atten;
    // Optional standalone SDI grid; caller retains these until end().
    const uint64_t *frequencies;
    const uint8_t *masks;
} acquisition_config_t;

typedef struct {
    uint32_t session, point;
    uint64_t frequency, elapsed_us;
    enum { ACQ_START, ACQ_POINT, ACQ_DONE } kind;
    bool ok;
    rf_raw_measurement_t raw;
    rf_point_timing_t timing;
    acquisition_stats_t stats;
} acquisition_result_t;

// Core 0 API: exclusive RF ownership transfers to core 1 from begin to end.
void acquisition_init(void);
bool acquisition_begin(acquisition_config_t config, uint64_t *startup_us);
bool acquisition_next(acquisition_result_t *result);
uint64_t acquisition_end(void); // cancel/drain; waits for RF-off acknowledgement
void acquisition_cancel(void);
bool acquisition_cancelled(void); // core 1 lock/queue waits
acquisition_stats_t acquisition_stats(void); // core 0, after end()
#endif
