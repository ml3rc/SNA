#include "protocol.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "pico/bootrom.h"
#include "pico/stdlib.h"
#include "tusb.h"

#include "eeprom.h"
#include "frequency_limits.h"
#include "main.h"
#include "measurement.h"
#include "rf_task.h"
#include "sdi_test.h"

#define FW_VERSION "0.4.1"
#define PROTO_VERSION 3
#define RESP_BUF_SIZE 8192
#define PENDING_MAX 8          // requests waiting for core 1's reply
#define WRITE_STALL_MS 2000    // a host that reads nothing for this long ends a sweep

#define POWER_INTERVAL_DEFAULT_MS 250
#define SLEEP_UNTIL_BOOTSEL_RESET 50
#define ATTEN_MAX 127              // HMC1119 code, 0.25 dB per step
#define SWEEP_POINTS_MAX 4096

#define S_PARAM_NAMES MEASUREMENT_SPARAM_NAMES
static const char *const PATH_NAMES[RF_APP_PATH_COUNT] = {
    [RF_APP_PATH_PORT1_FWD] = "p1_fwd", [RF_APP_PATH_PORT1_BWD] = "p1_bwd",
    [RF_APP_PATH_PORT2_FWD] = "p2_fwd", [RF_APP_PATH_PORT2_BWD] = "p2_bwd",
};
// By set index (eeprom_cal_set_index()): the S-parameters at 50, then at 75 ohm.
static const char *const CAL_SET_NAMES[] = {
    "s11_50", "s21_50", "s12_50", "s22_50",
    "s11_75", "s21_75", "s12_75", "s22_75",
};
_Static_assert(count_of(CAL_SET_NAMES) == EEPROM_CAL_SETS, "a name per calibration set");

static char resp_buf[RESP_BUF_SIZE];
static rf_status_t rf_status; // core 1's state after its latest final reply

// What to do with a reply once it arrives.
typedef enum {
    PENDING_REPLY,       // answer the host's request
    PENDING_BOOTSEL,     // mode left: answer, then reboot into the bootloader
    PENDING_POWER_EVENT, // continuous power meter reading -> event
    PENDING_SILENT,      // our own mode_exit after the host went away
} pending_purpose_t;

typedef struct {
    uint32_t tag;          // 0: free
    pending_purpose_t purpose;
    cJSON *req;            // copy of the host's request (id, arguments), NULL for our own
    uint32_t aux;          // per command: probe path mask, power interval
} pending_t;
static pending_t pending[PENDING_MAX];

// The USB sweep in progress (at most one: other commands are busy meanwhile).
typedef struct {
    bool active;
    bool stalled;          // the host stopped reading: skip further writes
    uint32_t tag;
    cJSON *req;
    uint8_t sparams, path_mask, z;
    uint32_t unlocked_points;
} usb_sweep_t;
static usb_sweep_t usb_sweep;

// Continuous power meter (power_start): event cadence.
static uint32_t power_interval_ms = POWER_INTERVAL_DEFAULT_MS;
static uint64_t power_next_us;
static bool power_sample_pending;
static bool host_exit_pending; // our mode_exit after a disconnect is on its way

// src_temp_read: the last live reading, returned while a read is not possible.
static bool temp_cache_valid;
static double temp_cache_c;
static uint64_t temp_cache_us;

bool protocol_is_sweeping(void) {
    return usb_sweep.active || sdi_test_running();
}

bool protocol_rf_idle(void) {
    return rf_status.mode == RF_APP_IDLE;
}

// --- JSON output ---

// A write gave up partway through a line: the next line starts with a newline,
// so the host drops the fragment as one junk line instead of losing the next
// message glued onto it.
static bool tx_line_cut;
// The host stopped reading (a write stalled for WRITE_STALL_MS): later lines
// are dropped at once until the TX FIFO has room again, instead of every one
// of them blocking core 0 for the full stall time.
static bool tx_stalled;

// Writes `len` bytes over USB CDC. tud_cdc_write() only copies into TinyUSB's
// TX FIFO; bytes leave the device inside tud_task(), so it is pumped whenever
// the FIFO is full -- otherwise a sweep's lines after the first few are
// silently dropped (seen on real hardware). Returns the bytes written: fewer
// than `len` if the host disconnected or read nothing for WRITE_STALL_MS.
static size_t write_cdc(const char *buf, size_t len) {
    size_t written = 0;
    uint32_t last_progress = to_ms_since_boot(get_absolute_time());
    while (written < len && tud_cdc_connected()) {
        update_heartbeat();
        uint32_t chunk = tud_cdc_write(buf + written, (uint32_t) (len - written));
        written += chunk;
        if (chunk > 0) last_progress = to_ms_since_boot(get_absolute_time());
        if (to_ms_since_boot(get_absolute_time()) - last_progress >= WRITE_STALL_MS) {
            tx_stalled = true;
            break;
        }
        tud_cdc_write_flush();
        if (written < len) tud_task();
    }
    return written;
}

// Prints `resp` into the static line buffer and writes it as one line, then
// frees it. A host that stops reading ends a running sweep.
static void send_json(cJSON *resp) {
    if (!usb_sweep.stalled && cJSON_PrintPreallocated(resp, resp_buf, (int) sizeof(resp_buf) - 1, false)) {
        size_t len = strlen(resp_buf);
        resp_buf[len++] = '\n'; // printed into sizeof - 1, so there is room
        // A stalled host gets the line only once it reads again.
        if (tx_stalled && tud_cdc_write_available() > 0) tx_stalled = false;
        if (!tx_stalled && tud_cdc_connected()) {
            if (tx_line_cut && write_cdc("\n", 1) == 1) tx_line_cut = false;
            if (!tx_line_cut) {
                size_t written = write_cdc(resp_buf, len);
                if (written > 0 && written < len) tx_line_cut = true;
            }
        }
        if (tx_stalled && usb_sweep.active) {
            usb_sweep.stalled = true;
            rf_task_cancel();
        }
    } // else: overflow -- shouldn't happen at RESP_BUF_SIZE, drop rather than send truncated JSON
    cJSON_Delete(resp);
}

void protocol_host_disconnected(void) {
    // The next session starts on a clean line.
    tx_line_cut = false;
    tx_stalled = false;
}

void protocol_send_event(cJSON *evt) {
    send_json(evt);
}

// Starts a response object, echoing `req`'s "id" verbatim (any JSON type).
// `req` may be NULL (the line was no JSON at all, so there is no id).
static cJSON *begin_resp(const cJSON *req) {
    cJSON *resp = cJSON_CreateObject();
    if (req) {
        const cJSON *id = cJSON_GetObjectItemCaseSensitive(req, "id");
        if (id) cJSON_AddItemToObject(resp, "id", cJSON_Duplicate(id, true));
    }
    return resp;
}

static cJSON *begin_ok(const cJSON *req) {
    cJSON *resp = begin_resp(req);
    cJSON_AddBoolToObject(resp, "ok", true);
    return resp;
}

static void send_ok(const cJSON *req) {
    send_json(begin_ok(req));
}

static cJSON *begin_err(const cJSON *req, const char *error) {
    cJSON *resp = begin_resp(req);
    cJSON_AddBoolToObject(resp, "ok", false);
    cJSON_AddStringToObject(resp, "error", error);
    return resp;
}

static void send_err(const cJSON *req, const char *error) {
    send_json(begin_err(req, error));
}

void protocol_reject_line(void) {
    send_err(NULL, "bad_request");
}

// `bad_args` naming the offending field.
static void send_bad_args(const cJSON *req, const char *field) {
    cJSON *resp = begin_err(req, "bad_args");
    cJSON_AddStringToObject(resp, "field", field);
    send_json(resp);
}

// Wire name of an application mode (mode_enter, status, wrong_mode replies).
static const char *mode_name(rf_app_mode_t mode) {
    switch (mode) {
        case RF_APP_SWEEP: return "sweep";
        case RF_APP_GENERATOR: return "generator";
        case RF_APP_PWR_DETECTOR: return "power_meter";
        case RF_APP_MANUAL: return "manual";
        default: return "idle";
    }
}

static bool mode_from_name(const char *name, rf_app_mode_t *mode) {
    const rf_app_mode_t modes[] = { RF_APP_SWEEP, RF_APP_GENERATOR, RF_APP_PWR_DETECTOR, RF_APP_MANUAL };
    for (size_t i = 0; i < count_of(modes); i++) {
        if (strcmp(name, mode_name(modes[i])) == 0) { *mode = modes[i]; return true; }
    }
    return false;
}

// Maps a refusal from core 1 to the wire error. Mode errors carry the current
// mode, so the host can tell what it has to exit first. Arguments are checked
// here before anything is sent, so BAD_ARGS from core 1 does not happen.
static void send_app_err(const cJSON *req, const rf_reply_t *r) {
    const char *error;
    switch (r->result) {
        case RF_APP_ERR_BUSY: error = "busy"; break;
        case RF_APP_ERR_MODE_ACTIVE: error = "mode_active"; break;
        case RF_APP_ERR_WRONG_MODE: error = "wrong_mode"; break;
        case RF_APP_ERR_OUTPUT_ACTIVE: error = "output_active"; break;
        case RF_APP_ERR_TUNE_FAILED: error = "tune_failed"; break;
        case RF_APP_ERR_NOT_LOCKED: error = "source_not_locked"; break;
        case RF_APP_ERR_NOT_VALID: error = "not_valid"; break;
        default: error = "bad_args"; break;
    }
    cJSON *resp = begin_err(req, error);
    if (r->result == RF_APP_ERR_MODE_ACTIVE || r->result == RF_APP_ERR_WRONG_MODE)
        cJSON_AddStringToObject(resp, "mode", mode_name(r->status.mode));
    send_json(resp);
}

// cJSON prints the shortest round-trip representation; rounding the value
// first keeps dB/volts/temperature readings at a fixed number of decimals.
static double round_dp(double v, int decimals) {
    double scale = pow(10.0, decimals);
    return round(v * scale) / scale;
}

// Frequencies are written as plain integers: cJSON's number printing can
// switch to exponent notation for large values, which the client rejects.
static void add_freq(cJSON *object, const char *name, uint64_t freq_hz) {
    char buf[21]; // up to 20 digits for a uint64_t + NUL
    snprintf(buf, sizeof(buf), "%llu", (unsigned long long) freq_hz);
    cJSON_AddRawToObject(object, name, buf);
}

// --- argument validation ---
// JSON numbers are checked before narrowing: out-of-range float-to-int
// conversions are undefined in C. Frequencies and counts must be integers.

static bool integer_in_range(const cJSON *item, double minimum, double maximum) {
    return cJSON_IsNumber(item) && isfinite(item->valuedouble) &&
           item->valuedouble >= minimum && item->valuedouble <= maximum &&
           floor(item->valuedouble) == item->valuedouble;
}

static bool valid_frequency(const cJSON *item) {
    return integer_in_range(item, (double) SNA_FREQ_MIN_HZ, (double) SNA_FREQ_MAX_HZ);
}

static bool valid_z(const cJSON *item) {
    return integer_in_range(item, 50, 75) && (item->valuedouble == 50 || item->valuedouble == 75);
}

static bool valid_atten(const cJSON *item) {
    return integer_in_range(item, 0, ATTEN_MAX);
}

static bool valid_port(const cJSON *item) {
    return integer_in_range(item, 1, 2);
}

// A dB value that fits the EEPROM's storage format (calibration, SDI thresholds).
static bool valid_db(const cJSON *item) {
    return cJSON_IsNumber(item) && isfinite(item->valuedouble) &&
           item->valuedouble >= EEPROM_DB_MIN && item->valuedouble <= EEPROM_DB_MAX;
}

static const cJSON *field(const cJSON *req, const char *name) {
    return cJSON_GetObjectItemCaseSensitive(req, name);
}

static int name_index(const char *const *names, int count, const cJSON *item) {
    if (!cJSON_IsString(item)) return -1;
    for (int i = 0; i < count; i++) {
        if (strcmp(item->valuestring, names[i]) == 0) return i;
    }
    return -1;
}

// "f0" and "f1" of a frequency span, f0 <= f1. Sends bad_args and returns
// false if either is invalid.
static bool check_span(const cJSON *req, const cJSON **f0, const cJSON **f1) {
    *f0 = field(req, "f0");
    *f1 = field(req, "f1");
    if (!valid_frequency(*f0)) { send_bad_args(req, "f0"); return false; }
    if (!valid_frequency(*f1) || (*f1)->valuedouble < (*f0)->valuedouble) { send_bad_args(req, "f1"); return false; }
    return true;
}

// --- requests to core 1 ---

static pending_t *find_pending(uint32_t tag) {
    for (size_t i = 0; i < PENDING_MAX; i++) {
        if (pending[i].tag == tag) return &pending[i];
    }
    return NULL;
}

// Sends `rf` to core 1 and remembers how to answer. False (and `busy` sent to
// the host) when too many requests are waiting.
static bool forward(const cJSON *req, rf_req_t *rf, pending_purpose_t purpose, uint32_t aux) {
    pending_t *slot = find_pending(0);
    uint32_t tag = slot ? rf_task_send(rf) : 0;
    if (tag == 0) {
        if (req) send_err(req, "busy");
        return false;
    }
    *slot = (pending_t) { .tag = tag, .purpose = purpose, .req = req ? cJSON_Duplicate(req, true) : NULL, .aux = aux };
    return true;
}

static void forward_simple(const cJSON *req, rf_req_kind_t kind) {
    rf_req_t rf = { .kind = kind };
    forward(req, &rf, PENDING_REPLY, 0);
}

// --- commands ---

static void cmd_ping(const cJSON *req) {
    cJSON *resp = begin_ok(req);
    cJSON_AddStringToObject(resp, "fw", FW_VERSION);
    cJSON_AddNumberToObject(resp, "proto", PROTO_VERSION);
    cJSON_AddNumberToObject(resp, "hw_rev", eeprom_hw_revision());
    add_freq(resp, "freq_min_hz", SNA_FREQ_MIN_HZ);
    add_freq(resp, "freq_max_hz", SNA_FREQ_MAX_HZ);
    send_json(resp);
}

static void cmd_set(const cJSON *req) {
    const cJSON *z = field(req, "z");
    const cJSON *atten = field(req, "atten");
    if (!z && !atten) { send_bad_args(req, "z"); return; }
    if (z && !valid_z(z)) { send_bad_args(req, "z"); return; }
    if (atten && !valid_atten(atten)) { send_bad_args(req, "atten"); return; }
    rf_req_t rf = { .kind = RF_REQ_SET };
    rf.arg.set.has_z = z != NULL;
    rf.arg.set.has_atten = atten != NULL;
    if (z) rf.arg.set.z = (uint8_t) z->valuedouble;
    if (atten) rf.arg.set.atten = (uint8_t) atten->valuedouble;
    forward(req, &rf, PENDING_REPLY, 0);
}

static void cmd_mode_enter(const cJSON *req) {
    rf_app_mode_t mode;
    const cJSON *name = field(req, "mode");
    if (!cJSON_IsString(name) || !mode_from_name(name->valuestring, &mode)) { send_bad_args(req, "mode"); return; }
    rf_req_t rf = { .kind = RF_REQ_MODE_ENTER, .arg.mode = mode };
    forward(req, &rf, PENDING_REPLY, 0);
}

// Back to idle from any mode, cancelling whatever runs (a sweep ends first,
// its final reply goes out before this one); also fine when already idle.
static void cmd_mode_exit(const cJSON *req) {
    rf_task_cancel();
    sdi_test_cancel();
    forward_simple(req, RF_REQ_MODE_EXIT);
}

static void cmd_abort(const cJSON *req) {
    rf_task_cancel();
    sdi_test_cancel();
    send_ok(req);
}

static void cmd_sweep(const cJSON *req) {
    const cJSON *f0, *f1;
    if (!check_span(req, &f0, &f1)) return;
    const cJSON *points = field(req, "points");
    if (!integer_in_range(points, 1, SWEEP_POINTS_MAX)) { send_bad_args(req, "points"); return; }
    const cJSON *log_item = field(req, "log");
    if (log_item && !cJSON_IsBool(log_item)) { send_bad_args(req, "log"); return; }
    const cJSON *z = field(req, "z");
    if (!valid_z(z)) { send_bad_args(req, "z"); return; }
    const cJSON *atten = field(req, "atten");
    if (!valid_atten(atten)) { send_bad_args(req, "atten"); return; }
    const cJSON *unlocked = field(req, "allow_unlocked");
    if (unlocked && !cJSON_IsBool(unlocked)) { send_bad_args(req, "allow_unlocked"); return; }

    // S-parameters or coupler paths (raw detector sweep), not both.
    uint8_t sparams = 0, path_mask = 0;
    const cJSON *params = field(req, "params"), *p;
    if (!cJSON_IsArray(params) || cJSON_GetArraySize(params) == 0) { send_bad_args(req, "params"); return; }
    cJSON_ArrayForEach(p, params) {
        int s = name_index(S_PARAM_NAMES, MEASUREMENT_SPARAMS, p);
        int path = name_index(PATH_NAMES, RF_APP_PATH_COUNT, p);
        if (s >= 0) sparams |= (uint8_t) (1u << s);
        else if (path >= 0) path_mask |= (uint8_t) (1u << path);
        else { send_bad_args(req, "params"); return; }
    }
    if (sparams && path_mask) { send_bad_args(req, "params"); return; }
    const cJSON *port = field(req, "port");
    if (path_mask ? !valid_port(port) : port != NULL) { send_bad_args(req, "port"); return; }

    rf_req_t rf = { .kind = RF_REQ_SWEEP, .arg.sweep = {
        .f0 = (uint64_t) f0->valuedouble, .f1 = (uint64_t) f1->valuedouble,
        .points = (uint32_t) points->valuedouble, .logarithmic = cJSON_IsTrue(log_item),
        .z = (uint8_t) z->valuedouble, .atten = (uint8_t) atten->valuedouble,
        .sparams = sparams, .path_mask = path_mask, .port = port ? (uint8_t) port->valuedouble : 0,
        .allow_unlocked = !cJSON_IsFalse(unlocked),
    } };
    eeprom_cal_prefetch(); // the points are calibrated from RAM only
    uint32_t tag = rf_task_send(&rf);
    if (tag == 0) { send_err(req, "busy"); return; }
    usb_sweep = (usb_sweep_t) {
        .active = true, .tag = tag, .req = cJSON_Duplicate(req, true),
        .sparams = sparams, .path_mask = path_mask, .z = rf.arg.sweep.z,
    };
}

static int cal_set_index(const cJSON *item) {
    return name_index(CAL_SET_NAMES, EEPROM_CAL_SETS, item);
}

static void cmd_cal_set(const cJSON *req) {
    int idx = cal_set_index(field(req, "set"));
    if (idx < 0) { send_bad_args(req, "set"); return; }
    const cJSON *f0, *f1;
    if (!check_span(req, &f0, &f1)) return;
    const cJSON *values = field(req, "points");
    int n = cJSON_IsArray(values) ? cJSON_GetArraySize(values) : 0;
    if (n <= 0 || n > EEPROM_CAL_POINTS_MAX) { send_bad_args(req, "points"); return; }
    static double points[EEPROM_CAL_POINTS_MAX];
    for (int i = 0; i < n; i++) {
        const cJSON *v = cJSON_GetArrayItem(values, i);
        if (!valid_db(v)) { send_bad_args(req, "points"); return; }
        points[i] = v->valuedouble;
    }
    switch (eeprom_cal_set(idx, (uint64_t) f0->valuedouble, (uint64_t) f1->valuedouble, points, (size_t) n)) {
        case EEPROM_CAL_OK: send_ok(req); break;
        case EEPROM_CAL_GRID_MISMATCH: send_err(req, "grid_mismatch"); break;
        case EEPROM_CAL_STORAGE_ERROR: send_err(req, "storage_error"); break;
        default: send_bad_args(req, "points"); break;
    }
}

static void cmd_cal_get(const cJSON *req) {
    int idx = cal_set_index(field(req, "set"));
    if (idx < 0) { send_bad_args(req, "set"); return; }
    static double points[EEPROM_CAL_POINTS_MAX];
    uint64_t f0, f1;
    size_t count;
    if (!eeprom_cal_get(idx, &f0, &f1, points, &count)) { send_err(req, "not_calibrated"); return; }
    cJSON *resp = begin_ok(req);
    cJSON_AddStringToObject(resp, "set", CAL_SET_NAMES[idx]);
    add_freq(resp, "f0", f0);
    add_freq(resp, "f1", f1);
    cJSON *arr = cJSON_AddArrayToObject(resp, "points");
    for (size_t i = 0; i < count; i++) cJSON_AddItemToArray(arr, cJSON_CreateNumber(round_dp(points[i], 2)));
    send_json(resp);
}

static void cmd_cal_clear(const cJSON *req) {
    const cJSON *set = field(req, "set");
    bool stored;
    if (cJSON_IsString(set) && strcmp(set->valuestring, "all") == 0) {
        stored = eeprom_cal_clear_all();
    } else {
        int idx = cal_set_index(set);
        if (idx < 0) { send_bad_args(req, "set"); return; }
        stored = eeprom_cal_clear(idx);
    }
    if (stored) send_ok(req);
    else send_err(req, "storage_error");
}

static void cmd_sdi_test_set(const cJSON *req) {
    const cJSON *arr = field(req, "points");
    if (!cJSON_IsArray(arr) || cJSON_GetArraySize(arr) > EEPROM_SDI_POINTS_MAX) { send_bad_args(req, "points"); return; }
    static sdi_test_point_t points[EEPROM_SDI_POINTS_MAX];
    size_t count = 0;
    const cJSON *elem;
    cJSON_ArrayForEach(elem, arr) {
        const cJSON *f = field(elem, "f"), *thresh = field(elem, "thresh_db");
        int sp = name_index(S_PARAM_NAMES, MEASUREMENT_SPARAMS, field(elem, "s_param"));
        const char *const cmps[] = { "lt", "gt" };
        int cmp = name_index(cmps, 2, field(elem, "cmp"));
        if (!valid_frequency(f) || !valid_db(thresh) || sp < 0 || cmp < 0) { send_bad_args(req, "points"); return; }
        points[count++] = (sdi_test_point_t) {
            .freq_hz = (uint64_t) f->valuedouble,
            .thresh_db_x100 = (int16_t) lround(thresh->valuedouble * 100.0),
            .cmp_gt = cmp == 1,
            .s_param = (uint8_t) sp,
        };
    }
    if (eeprom_sdi_test_set(points, count)) send_ok(req);
    else send_err(req, "storage_error");
}

static void cmd_sdi_test_get(const cJSON *req) {
    static sdi_test_point_t points[EEPROM_SDI_POINTS_MAX];
    size_t count;
    eeprom_sdi_test_get(points, &count);
    cJSON *resp = begin_ok(req);
    cJSON *arr = cJSON_AddArrayToObject(resp, "points");
    for (size_t i = 0; i < count; i++) {
        cJSON *pt = cJSON_CreateObject();
        add_freq(pt, "f", points[i].freq_hz);
        cJSON_AddNumberToObject(pt, "thresh_db", round_dp(points[i].thresh_db_x100 / 100.0, 2));
        cJSON_AddStringToObject(pt, "s_param", S_PARAM_NAMES[points[i].s_param]); // checked by eeprom_sdi_test_get()
        cJSON_AddStringToObject(pt, "cmp", points[i].cmp_gt ? "gt" : "lt");
        cJSON_AddItemToArray(arr, pt);
    }
    send_json(resp);
}

static void cmd_manual(const cJSON *req) {
    static const char *const blocks[] = {
        [RF_MANUAL_LPF] = "lpf", [RF_MANUAL_COUPLER] = "cplr", [RF_MANUAL_PORT] = "port",
        [RF_MANUAL_SRC_FREQ] = "src_freq", [RF_MANUAL_SRC_EN] = "src_en",
    };
    _Static_assert(count_of(blocks) == RF_MANUAL_COUNT, "a name per manual block");
    int block = name_index(blocks, RF_MANUAL_COUNT, field(req, "block"));
    if (block < 0) { send_bad_args(req, "block"); return; }
    const cJSON *value = field(req, "value");
    rf_req_t rf = { .kind = RF_REQ_MANUAL, .arg.manual.block = (rf_manual_block_t) block };
    bool ok;
    switch (rf.arg.manual.block) {
        case RF_MANUAL_LPF: ok = integer_in_range(value, 0, RF_APP_LPF_COUNT - 1); break;
        case RF_MANUAL_COUPLER: ok = integer_in_range(value, 0, RF_APP_PATH_COUNT - 1); break;
        case RF_MANUAL_PORT: ok = integer_in_range(value, 0, 2); break;
        case RF_MANUAL_SRC_FREQ: ok = valid_frequency(value); break;
        default: ok = cJSON_IsBool(value); break;
    }
    if (!ok) { send_bad_args(req, "value"); return; }
    rf.arg.manual.value = cJSON_IsBool(value) ? (uint64_t) cJSON_IsTrue(value) : (uint64_t) value->valuedouble;
    forward(req, &rf, PENDING_REPLY, (uint32_t) block); // the reply needs to know which
}

static void cmd_gen_start(const cJSON *req) {
    const cJSON *port = field(req, "port"), *z = field(req, "z"), *atten = field(req, "atten"), *f = field(req, "f");
    if (!valid_port(port)) { send_bad_args(req, "port"); return; }
    if (!valid_z(z)) { send_bad_args(req, "z"); return; }
    if (!valid_atten(atten)) { send_bad_args(req, "atten"); return; }
    if (!valid_frequency(f)) { send_bad_args(req, "f"); return; }
    rf_req_t rf = { .kind = RF_REQ_GEN_START, .arg.gen = {
        .port = (uint8_t) port->valuedouble, .z = (uint8_t) z->valuedouble,
        .atten = (uint8_t) atten->valuedouble, .freq_hz = (uint64_t) f->valuedouble,
    } };
    forward(req, &rf, PENDING_REPLY, 0);
}

static void cmd_gen_stop(const cJSON *req) { forward_simple(req, RF_REQ_GEN_STOP); }

// power_read / power_start: {"port","z"} (+ "interval_ms" for power_start).
static void cmd_power(const cJSON *req, rf_req_kind_t kind) {
    const cJSON *port = field(req, "port"), *z = field(req, "z"), *interval = field(req, "interval_ms");
    if (!valid_port(port)) { send_bad_args(req, "port"); return; }
    if (!valid_z(z)) { send_bad_args(req, "z"); return; }
    if (kind == RF_REQ_POWER_START && interval && !integer_in_range(interval, 20, 10000)) {
        send_bad_args(req, "interval_ms");
        return;
    }
    rf_req_t rf = { .kind = kind, .arg.power = { .port = (uint8_t) port->valuedouble, .z = (uint8_t) z->valuedouble } };
    // aux: the port for power_read's reply, the interval for power_start's.
    uint32_t aux = kind == RF_REQ_POWER_READ ? rf.arg.power.port
                 : interval ? (uint32_t) interval->valuedouble : POWER_INTERVAL_DEFAULT_MS;
    forward(req, &rf, PENDING_REPLY, aux);
}

static void cmd_power_read(const cJSON *req) { cmd_power(req, RF_REQ_POWER_READ); }
static void cmd_power_start(const cJSON *req) { cmd_power(req, RF_REQ_POWER_START); }
static void cmd_power_stop(const cJSON *req) { forward_simple(req, RF_REQ_POWER_STOP); }

static void cmd_probe_read(const cJSON *req) {
    uint8_t mask = RF_APP_PATH_MASK_ALL;
    const cJSON *paths = field(req, "paths");
    if (paths) {
        if (!cJSON_IsArray(paths) || cJSON_GetArraySize(paths) < 1) { send_bad_args(req, "paths"); return; }
        mask = 0;
        const cJSON *p;
        cJSON_ArrayForEach(p, paths) {
            if (!integer_in_range(p, 0, RF_APP_PATH_COUNT - 1) || (mask & (1u << (int) p->valuedouble))) {
                send_bad_args(req, "paths");
                return;
            }
            mask |= (uint8_t) (1u << (int) p->valuedouble);
        }
    }
    rf_req_t rf = { .kind = RF_REQ_PROBE, .arg.path_mask = mask };
    forward(req, &rf, PENDING_REPLY, mask);
}

static void reply_temp(const cJSON *req, bool cached, double temp_c) {
    if (cached && !temp_cache_valid) { send_err(req, "not_valid"); return; }
    cJSON *resp = begin_ok(req);
    cJSON_AddBoolToObject(resp, "cached", cached);
    if (cached) cJSON_AddNumberToObject(resp, "age_ms", (double) ((time_us_64() - temp_cache_us) / 1000u));
    cJSON_AddNumberToObject(resp, "temp_c", round_dp(cached ? temp_cache_c : temp_c, 1));
    send_json(resp);
}

// The read rewrites MAX2871 registers, which must not happen under a live
// signal or a measurement: then the last live value is returned.
static void cmd_src_temp_read(const cJSON *req) {
    if (protocol_is_sweeping()) reply_temp(req, true, 0.0);
    else forward_simple(req, RF_REQ_TEMP);
}

static void cmd_status(const cJSON *req) { forward_simple(req, RF_REQ_STATUS); }

static void cmd_bootsel(const cJSON *req) {
    rf_task_cancel(); // never leave anything running across the reboot
    rf_req_t rf = { .kind = RF_REQ_MODE_EXIT };
    forward(req, &rf, PENDING_BOOTSEL, 0);
}

// --- replies from core 1 ---

// AD8318 usable range is about -55..0 dBm at the detector (docs-local/RF.drawio).
static const char *dbm_range(double dbm) {
    return dbm < -55.0 ? "low" : (dbm > 0.0 ? "high" : "ok");
}

// "volts", "dbm" and "range" of one detector reading.
static void add_detector_levels(cJSON *obj, uint16_t raw) {
    cJSON_AddNumberToObject(obj, "volts", round_dp(rf_app_counts_to_volts(raw), 3));
    double dbm = rf_app_counts_to_dbm(raw);
    cJSON_AddNumberToObject(obj, "dbm", round_dp(dbm, 1));
    cJSON_AddStringToObject(obj, "range", dbm_range(dbm));
}

static void add_power_reading(cJSON *obj, uint8_t port, uint8_t z, uint16_t raw) {
    cJSON_AddNumberToObject(obj, "port", port);
    cJSON_AddNumberToObject(obj, "z", z);
    cJSON_AddNumberToObject(obj, "raw", raw);
    add_detector_levels(obj, raw);
}

static void reply_status(const cJSON *req, const rf_status_t *s) {
    // init_error_t bit order; USB is left out: without it there is no host to tell.
    static const char *const init_error_names[] = { "eeprom", "source_lock", "detector" };
    _Static_assert(count_of(init_error_names) == INIT_ERROR_CODES - 1 && INIT_ERR_DETECTOR == 1u << 2,
                   "a name per init_error_t bit but INIT_ERR_USB");
    cJSON *resp = begin_ok(req);
    cJSON_AddStringToObject(resp, "mode", mode_name(s->mode));
    cJSON_AddBoolToObject(resp, "sweeping", protocol_is_sweeping());
    cJSON_AddNumberToObject(resp, "z", s->z);
    cJSON_AddNumberToObject(resp, "port", s->port);
    cJSON_AddNumberToObject(resp, "atten", s->atten);
    cJSON_AddBoolToObject(resp, "gen_on", s->gen_on);
    if (s->gen_on) add_freq(resp, "gen_f", s->gen_freq);
    cJSON_AddBoolToObject(resp, "pwr_on", s->pwr_on);
    if (s->pwr_on) cJSON_AddNumberToObject(resp, "pwr_port", s->pwr_port);
    cJSON *errors = cJSON_AddArrayToObject(resp, "init_errors");
    uint8_t bits = get_init_errors();
    for (size_t i = 0; i < count_of(init_error_names); i++) {
        if (bits & (1u << i)) cJSON_AddItemToArray(errors, cJSON_CreateString(init_error_names[i]));
    }
    send_json(resp);
}

static void reply_probe(const cJSON *req, const rf_reply_t *r, uint8_t mask) {
    const rf_status_t *s = &r->status;
    cJSON *resp = begin_ok(req);
    cJSON_AddBoolToObject(resp, "gen_on", s->gen_on);
    cJSON_AddNumberToObject(resp, "port", s->port);
    cJSON_AddNumberToObject(resp, "z", s->z);
    cJSON_AddNumberToObject(resp, "atten", s->atten);
    if (s->gen_on) add_freq(resp, "f", s->gen_freq);
    cJSON *readings = cJSON_AddArrayToObject(resp, "readings");
    uint16_t path_raw[RF_APP_PATH_COUNT] = {0};
    for (int path = 0; path < RF_APP_PATH_COUNT; path++) {
        if (!(mask & (1u << path))) continue;
        const rf_app_detector_sample_t *sample = &r->data.probe[path];
        path_raw[path] = sample->median;
        cJSON *reading = cJSON_CreateObject();
        cJSON_AddNumberToObject(reading, "path", path);
        cJSON_AddStringToObject(reading, "name", PATH_NAMES[path]);
        cJSON_AddNumberToObject(reading, "raw", sample->median);
        cJSON_AddNumberToObject(reading, "min", sample->min);
        cJSON_AddNumberToObject(reading, "max", sample->max);
        add_detector_levels(reading, sample->median);
        cJSON_AddItemToArray(readings, reading);
    }

    // The S-parameter pair the generator's source port measures, if its three paths were read.
    rf_app_raw_measurement_t raw;
    if (s->gen_on && rf_app_raw_from_paths(s->port, path_raw, mask, &raw)) {
        int refl = s->port == 1 ? MEASUREMENT_IDX_S11 : MEASUREMENT_IDX_S22;
        int trans = s->port == 1 ? MEASUREMENT_IDX_S21 : MEASUREMENT_IDX_S12;
        uint16_t ref = s->port == 1 ? raw.source1_ref : raw.source2_ref;
        double refl_db = rf_app_counts_ratio_db(s->port == 1 ? raw.source1_reflection : raw.source2_reflection, ref);
        double trans_db = rf_app_counts_ratio_db(s->port == 1 ? raw.source1_transmission : raw.source2_transmission, ref);
        eeprom_cal_prefetch();
        measurement_t m = measurement_process(&raw, s->gen_freq, s->z, (uint8_t) ((1u << refl) | (1u << trans)));
        cJSON *d = cJSON_AddObjectToObject(resp, "derived");
        cJSON_AddStringToObject(d, "refl", S_PARAM_NAMES[refl]);
        cJSON_AddNumberToObject(d, "refl_db", round_dp(refl_db, 2));
        cJSON_AddNumberToObject(d, "refl_cal_db", round_dp(m.db[refl], 2));
        cJSON_AddStringToObject(d, "trans", S_PARAM_NAMES[trans]);
        cJSON_AddNumberToObject(d, "trans_db", round_dp(trans_db, 2));
        cJSON_AddNumberToObject(d, "trans_cal_db", round_dp(m.db[trans], 2));
        cJSON_AddBoolToObject(d, "cal", m.calibrated);
    }
    send_json(resp);
}

// The answer to the host's request `req`, from core 1's final reply.
static void reply_request(const cJSON *req, const rf_reply_t *r, uint32_t aux) {
    if (r->kind == RF_REQ_TEMP) {
        if (r->result == RF_APP_OK) {
            temp_cache_valid = true;
            temp_cache_c = r->data.temp_c;
            temp_cache_us = time_us_64();
            reply_temp(req, false, r->data.temp_c);
        } else if (r->result == RF_APP_ERR_NOT_VALID) {
            send_err(req, "not_valid");
        } else {
            reply_temp(req, true, 0.0); // source on or measuring: the last live value
        }
        return;
    }
    if (r->result != RF_APP_OK) { send_app_err(req, r); return; }

    cJSON *resp;
    switch (r->kind) {
        case RF_REQ_STATUS:
            reply_status(req, &r->status);
            return;
        case RF_REQ_MODE_ENTER:
        case RF_REQ_MODE_EXIT:
            resp = begin_ok(req);
            cJSON_AddStringToObject(resp, "mode", mode_name(r->status.mode));
            break;
        case RF_REQ_SET:
            resp = begin_ok(req);
            cJSON_AddNumberToObject(resp, "z", r->status.z);
            cJSON_AddNumberToObject(resp, "atten", r->status.atten);
            break;
        case RF_REQ_POWER_READ:
            resp = begin_ok(req);
            add_power_reading(resp, (uint8_t) aux, r->status.z, r->data.raw);
            break;
        case RF_REQ_POWER_START:
            power_interval_ms = aux;
            power_next_us = time_us_64(); // first event right away
            resp = begin_ok(req);
            cJSON_AddNumberToObject(resp, "port", r->status.pwr_port);
            cJSON_AddNumberToObject(resp, "z", r->status.z);
            cJSON_AddNumberToObject(resp, "interval_ms", power_interval_ms);
            break;
        case RF_REQ_PROBE:
            reply_probe(req, r, (uint8_t) aux);
            return;
        case RF_REQ_MANUAL:
            resp = begin_ok(req);
            if (aux == RF_MANUAL_PORT)
                cJSON_AddBoolToObject(resp, "source_on", r->status.source_on);
            break;
        default:
            resp = begin_ok(req);
            break;
    }
    send_json(resp);
}

static void handle_sweep_reply(const rf_reply_t *r) {
    const cJSON *req = usb_sweep.req;
    if (!r->final) {
        uint64_t f = r->data.point.freq_hz;
        const rf_app_raw_measurement_t *raw = &r->data.point.raw;
        measurement_t m = measurement_process(raw, f, usb_sweep.z, usb_sweep.sparams);
        if (!m.locked) usb_sweep.unlocked_points++;
        cJSON *resp = begin_resp(req);
        cJSON_AddNumberToObject(resp, "pt", r->data.point.index);
        add_freq(resp, "f", f);
        for (int i = 0; i < MEASUREMENT_SPARAMS; i++) {
            char key[8];
            snprintf(key, sizeof(key), "%s_db", S_PARAM_NAMES[i]);
            if (usb_sweep.sparams & (1u << i)) cJSON_AddNumberToObject(resp, key, round_dp(m.db[i], 2));
        }
        for (int path = 0; path < RF_APP_PATH_COUNT; path++) {
            if (!(usb_sweep.path_mask & (1u << path))) continue;
            char key[16];
            snprintf(key, sizeof(key), "%s_raw", PATH_NAMES[path]);
            cJSON_AddNumberToObject(resp, key, raw->path_raw[path]);
            snprintf(key, sizeof(key), "%s_dbm", PATH_NAMES[path]);
            cJSON_AddNumberToObject(resp, key, round_dp(rf_app_counts_to_dbm(raw->path_raw[path]), 1));
        }
        cJSON_AddBoolToObject(resp, "locked", m.locked);
        if (!m.calibrated) cJSON_AddBoolToObject(resp, "cal", false);
        send_json(resp);
        return;
    }

    if (r->result == RF_APP_ERR_WRONG_MODE) {
        send_app_err(req, r);
    } else {
        rf_sweep_end_t end = r->data.sweep.end;
        cJSON *resp = begin_resp(req);
        cJSON_AddBoolToObject(resp, "ok", end == RF_SWEEP_DONE);
        cJSON_AddBoolToObject(resp, "done", true);
        if (usb_sweep.unlocked_points) {
            cJSON_AddStringToObject(resp, "warning", "source_unlocked");
            cJSON_AddNumberToObject(resp, "unlocked_points", usb_sweep.unlocked_points);
        }
        if (end == RF_SWEEP_ABORTED) {
            cJSON_AddStringToObject(resp, "error", "aborted");
        } else if (end != RF_SWEEP_DONE) {
            cJSON_AddStringToObject(resp, "error", "source_not_locked");
            cJSON_AddStringToObject(resp, "stage", end == RF_SWEEP_STARTUP_FAILED ? "startup" : "retune");
            cJSON_AddNumberToObject(resp, "pt", r->data.sweep.index);
            add_freq(resp, "f", r->data.sweep.freq_hz);
        }
        usb_sweep.stalled = false; // the final reply is always attempted
        send_json(resp);
    }
    cJSON_Delete(usb_sweep.req);
    usb_sweep = (usb_sweep_t) {0};
}

static void handle_reply(const rf_reply_t *r) {
    if (usb_sweep.active && r->tag == usb_sweep.tag) {
        handle_sweep_reply(r);
        return;
    }
    pending_t *p = find_pending(r->tag);
    if (!p) return;
    pending_t entry = *p;
    p->tag = 0;
    switch (entry.purpose) {
        case PENDING_REPLY:
            reply_request(entry.req, r, entry.aux);
            break;
        case PENDING_BOOTSEL: {
            send_ok(entry.req);
            // Let the response leave over USB before we vanish: bytes only move inside tud_task().
            uint64_t until = time_us_64() + (uint64_t) SLEEP_UNTIL_BOOTSEL_RESET * 1000u;
            while (time_us_64() < until) {
                tud_task();
                tud_cdc_write_flush();
            }
            reset_usb_boot(0, 0);
            break;
        }
        case PENDING_POWER_EVENT:
            power_sample_pending = false;
            if (r->result == RF_APP_OK) {
                cJSON *evt = cJSON_CreateObject();
                cJSON_AddStringToObject(evt, "event", "power");
                add_power_reading(evt, r->status.pwr_port, r->status.z, r->data.raw);
                send_json(evt);
            }
            break;
        case PENDING_SILENT:
            host_exit_pending = false;
            break;
    }
    cJSON_Delete(entry.req);
}

// The continuous power meter: one reading per interval while it runs and a host listens.
static bool service_power_stream(void) {
    if (!rf_status.pwr_on || power_sample_pending || !tud_cdc_connected()) return false;
    uint64_t now = time_us_64();
    if (now < power_next_us) return false;
    // Fixed cadence; after a stall (a long command) restart it instead of catching up in a burst.
    power_next_us += (uint64_t) power_interval_ms * 1000u;
    if (power_next_us <= now) power_next_us = now + (uint64_t) power_interval_ms * 1000u;
    rf_req_t rf = { .kind = RF_REQ_POWER_SAMPLE };
    power_sample_pending = forward(NULL, &rf, PENDING_POWER_EVENT, 0);
    return power_sample_pending;
}

// A mode never outlives its controlling host, nor does a USB sweep. The
// on-board SDI test is host-independent and leaves its mode itself.
static bool service_host_gone(void) {
    if (tud_cdc_connected()) return false;
    if (usb_sweep.active && !usb_sweep.stalled) {
        usb_sweep.stalled = true; // nobody to write to
        rf_task_cancel();
        return true;
    }
    if (rf_status.mode != RF_APP_IDLE && !sdi_test_running() && !host_exit_pending && !usb_sweep.active) {
        rf_task_cancel();
        rf_req_t rf = { .kind = RF_REQ_MODE_EXIT };
        host_exit_pending = forward(NULL, &rf, PENDING_SILENT, 0);
        return host_exit_pending;
    }
    return false;
}

bool protocol_poll(void) {
    bool worked = false;
    rf_reply_t r;
    // At most one queue's worth per call: core 1 refills the queue as fast as core 0
    // drains it during a sweep, and the main loop must still get to read `abort`.
    for (unsigned n = 0; n < RF_TASK_REPLY_QUEUE_LENGTH && rf_task_poll(&r); n++) {
        worked = true;
        if (r.final) rf_status = r.status;
        if (!sdi_test_handle_reply(&r)) handle_reply(&r);
    }
    worked |= service_host_gone();
    worked |= service_power_stream();
    return worked;
}

// --- dispatch ---

typedef struct {
    const char *name;
    void (*handler)(const cJSON *req);
    bool while_sweeping; // allowed during a sweep or the SDI test
} command_t;

static const command_t commands[] = {
    { "ping", cmd_ping, true },
    { "status", cmd_status, true },
    { "set", cmd_set, true },
    { "abort", cmd_abort, true },
    { "mode_exit", cmd_mode_exit, true },
    { "src_temp_read", cmd_src_temp_read, true },
    { "mode_enter", cmd_mode_enter, false },
    { "sweep", cmd_sweep, false },
    { "gen_start", cmd_gen_start, false },
    { "gen_stop", cmd_gen_stop, false },
    { "power_read", cmd_power_read, false },
    { "power_start", cmd_power_start, false },
    { "power_stop", cmd_power_stop, false },
    { "probe_read", cmd_probe_read, false },
    { "manual", cmd_manual, false },
    { "cal_set", cmd_cal_set, false },
    { "cal_get", cmd_cal_get, false },
    { "cal_clear", cmd_cal_clear, false },
    { "sdi_test_set", cmd_sdi_test_set, false },
    { "sdi_test_get", cmd_sdi_test_get, false },
    { "bootsel", cmd_bootsel, false },
};

void protocol_handle_line(const char *line) {
    cJSON *req = cJSON_Parse(line);
    const cJSON *cmd = cJSON_IsObject(req) ? field(req, "cmd") : NULL;
    if (!cJSON_IsString(cmd)) {
        send_err(cJSON_IsObject(req) ? req : NULL, "bad_request");
        cJSON_Delete(req);
        return;
    }
    const command_t *command = NULL;
    for (size_t i = 0; i < count_of(commands) && !command; i++) {
        if (strcmp(cmd->valuestring, commands[i].name) == 0) command = &commands[i];
    }
    if (!command) send_err(req, "unknown_cmd");
    else if (protocol_is_sweeping() && !command->while_sweeping) send_err(req, "busy");
    else command->handler(req);
    cJSON_Delete(req);
}
