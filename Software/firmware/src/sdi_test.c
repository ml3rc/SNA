#include "sdi_test.h"

#include <math.h>
#include <stdio.h>

#include "pico/stdlib.h"
#include "tusb.h"

#include "eeprom.h"
#include "main.h"
#include "measurement.h"
#include "protocol.h"

#define RESULT_ON_MS 300   // one green/red pulse per point
#define RESULT_OFF_MS 150
#define REFUSED_MS 50     // one red pulse: not idle
#define NO_CONFIG_MS 50     // one orange pulse: no config
#define SDI_Z 75            // SDI is 75 ohm (BNC); left as the setting afterwards


typedef enum {
    SDI_IDLE,
    SDI_ENTERING,  // mode_enter(sweep) sent
    SDI_SWEEPING,
    SDI_EXITING,   // mode_exit sent after the sweep
    SDI_SHOWING,   // result pulses
    SDI_REFUSED,   // one red pulse
    SDI_NO_CONFIG, // one orange pulse
} sdi_state_t;

static struct {
    sdi_state_t state;
    uint32_t tag;       // of the request in flight
    uint32_t shown_ms;  // start of SDI_SHOWING / SDI_REFUSED
    bool aborted;
    size_t count;
    sdi_test_point_t points[EEPROM_SDI_POINTS_MAX];
    // The sweep's grid, kept until its final reply (rf_sweep_config_t).
    uint64_t freqs[EEPROM_SDI_POINTS_MAX];
    uint8_t sparams[EEPROM_SDI_POINTS_MAX];
    bool pass[EEPROM_SDI_POINTS_MAX];
    double val_db[EEPROM_SDI_POINTS_MAX];
} sdi;

static uint32_t now_ms(void) { return to_ms_since_boot(get_absolute_time()); }

static void show(sdi_state_t state) {
    sdi.state = state;
    sdi.shown_ms = now_ms();
}

static bool send(rf_req_t *req) {
    sdi.tag = rf_task_send(req);
    return sdi.tag != 0;
}

void sdi_test_start(void) {
    if (sdi.state != SDI_IDLE) return;
    eeprom_sdi_test_get(sdi.points, &sdi.count);
    if (sdi.count == 0) { show(SDI_NO_CONFIG); return; }
    // Like every mode, the test only starts from idle; core 1 decides. A mode
    // already known is refused right here: waiting for core 1's answer would
    // count as a running test and turn the host's commands away with `busy`
    // meanwhile (button presses during a generator readout stopped it).
    if (!protocol_rf_idle()) { show(SDI_REFUSED); return; }
    rf_req_t req = { .kind = RF_REQ_MODE_ENTER, .arg.mode = RF_APP_SWEEP };
    if (!send(&req)) { show(SDI_REFUSED); return; }
    sdi.state = SDI_ENTERING;
    sdi.aborted = false;
}

bool sdi_test_running(void) {
    return sdi.state == SDI_ENTERING || sdi.state == SDI_SWEEPING || sdi.state == SDI_EXITING;
}

static void start_sweep(void) {
    for (size_t i = 0; i < sdi.count; i++) {
        sdi.freqs[i] = sdi.points[i].freq_hz;
        sdi.sparams[i] = (uint8_t) (1u << sdi.points[i].s_param); // < MEASUREMENT_SPARAMS, checked by eeprom_sdi_test_get()
        sdi.pass[i] = false; // a point without a reading fails
        sdi.val_db[i] = 0.0;
    }
    eeprom_cal_prefetch();
    // 0 dB attenuation: the level the calibration curves are captured at.
    rf_req_t req = { .kind = RF_REQ_SWEEP, .arg.sweep = {
        .f0 = sdi.freqs[0], .f1 = sdi.freqs[sdi.count - 1], .points = (uint32_t) sdi.count,
        .z = SDI_Z, .atten = 0, .allow_unlocked = false, .continue_on_failure = true,
        .frequencies = sdi.freqs, .point_sparams = sdi.sparams,
    } };
    sdi.state = SDI_SWEEPING;
    if (!send(&req)) { sdi.aborted = true; sdi.state = SDI_IDLE; }
}

static void exit_mode(void) {
    rf_req_t req = { .kind = RF_REQ_MODE_EXIT };
    sdi.state = SDI_EXITING;
    if (!send(&req)) sdi.state = SDI_IDLE; // the host's next mode_exit (or a disconnect) cleans up
}

static void send_result(void) {
    if (!tud_cdc_connected()) return;
    bool overall_pass = true;
    cJSON *evt = cJSON_CreateObject();
    cJSON_AddStringToObject(evt, "event", "sdi_test_result");
    cJSON *results = cJSON_CreateArray();
    for (size_t i = 0; i < sdi.count; i++) {
        overall_pass = overall_pass && sdi.pass[i];
        cJSON *r = cJSON_CreateObject();
        char f[21];
        snprintf(f, sizeof(f), "%llu", (unsigned long long) sdi.points[i].freq_hz);
        cJSON_AddRawToObject(r, "f", f);
        cJSON_AddStringToObject(r, "s_param", MEASUREMENT_SPARAM_NAMES[sdi.points[i].s_param]);
        cJSON_AddNumberToObject(r, "val_db", round(sdi.val_db[i] * 100.0) / 100.0);
        cJSON_AddBoolToObject(r, "pass", sdi.pass[i]);
        cJSON_AddItemToArray(results, r);
    }
    cJSON_AddBoolToObject(evt, "pass", overall_pass);
    cJSON_AddItemToObject(evt, "results", results);
    protocol_send_event(evt);
}

static void handle_point(const rf_reply_t *r) {
    uint32_t i = r->data.point.index;
    if (i >= sdi.count) return;
    uint8_t sp = sdi.points[i].s_param;
    measurement_t m = measurement_process(&r->data.point.raw, r->data.point.freq_hz, SDI_Z, (uint8_t) (1u << sp));
    double threshold = sdi.points[i].thresh_db_x100 / 100.0;
    sdi.val_db[i] = m.db[sp];
    sdi.pass[i] = m.locked && (sdi.points[i].cmp_gt ? m.db[sp] > threshold : m.db[sp] < threshold);
}

void sdi_test_cancel(void) {
    if (sdi.state == SDI_ENTERING) sdi.aborted = true;
}

bool sdi_test_handle_reply(const rf_reply_t *r) {
    if (!sdi_test_running() || r->tag != sdi.tag) return false;
    switch (sdi.state) {
        case SDI_ENTERING:
            if (r->result != RF_APP_OK) show(SDI_REFUSED);
            else if (sdi.aborted) exit_mode();
            else start_sweep();
            break;
        case SDI_SWEEPING:
            if (!r->final) { handle_point(r); break; }
            sdi.aborted = sdi.aborted || r->data.sweep.end == RF_SWEEP_ABORTED;
            exit_mode();
            break;
        case SDI_EXITING:
            if (sdi.aborted) { sdi.state = SDI_IDLE; break; }
            send_result();
            show(SDI_SHOWING);
            break;
        default:
            break;
    }
    return true;
}

void sdi_test_update_leds(uint32_t now) {
    // `now` can be a little older than shown_ms (the main loop reads the time before the
    // button, which can refuse on the spot): that is the start, not 49 days later.
    int32_t since = (int32_t) (now - sdi.shown_ms);
    uint32_t t = since > 0 ? (uint32_t) since : 0;
    bool on;
    switch (sdi.state) {
        case SDI_REFUSED:
            on = t < REFUSED_MS;
            set_sdi_leds(false, on);
            if (!on) sdi.state = SDI_IDLE;
            break;

        case SDI_NO_CONFIG:
            on = t < NO_CONFIG_MS;
            set_sdi_leds(on, on);
            if (!on) sdi.state = SDI_IDLE;
            break;

        case SDI_SHOWING:
            uint32_t i = t / (RESULT_ON_MS + RESULT_OFF_MS);
            if (i >= sdi.count) {
                set_sdi_leds(false, false);
                sdi.state = SDI_IDLE;
                return;
            }
            on = t % (RESULT_ON_MS + RESULT_OFF_MS) < RESULT_ON_MS;
            set_sdi_leds(on && sdi.pass[i], on && !sdi.pass[i]);
            break; 

        default:
            break;
    }
}
