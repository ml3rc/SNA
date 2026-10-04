#include "frequency_limits.h"

#include "protocol.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "hardware/gpio.h"
#include "pico/bootrom.h"
#include "pico/stdlib.h"
#include "tusb.h"

#include "cJSON.h"
#include "main.h"
#include "rf_control.h"
#include "sna.h"
#ifdef SNA_MULTICORE
#include "acquisition.h"
#endif

#define FW_VERSION "0.1.11"
#define PROTO_VERSION 1
#define RESP_BUF_SIZE 8192

static char resp_buf[RESP_BUF_SIZE];
static volatile bool sweeping = false;
static volatile bool sweep_abort_requested = false;
static bool measurement_requires_usb = false;

static const char *CAL_SET_NAMES[8] = {
    "s11_50", "s21_50", "s12_50", "s22_50",
    "s11_75", "s21_75", "s12_75", "s22_75",
};

bool protocol_is_sweeping(void) {
    return sweeping;
}

bool protocol_measurement_cancelled(void) {
    if (sweeping && measurement_requires_usb && !tud_cdc_connected()) sweep_abort_requested = true;
    return sweeping && sweep_abort_requested;
}

static int cal_set_index(const char *name) {
    for (int i = 0; i < 8; i++) {
        if (strcmp(name, CAL_SET_NAMES[i]) == 0) return i;
    }
    return -1;
}

// Same 4 names/ordering as `sweep`'s `params` array and sdi_test_point_t's
// s_param field (0=s11, 1=s21, 2=s12, 3=s22) -- shared by cmd_sdi_test_set/
// cmd_sdi_test_get/protocol_run_sdi_self_test() below.
static const char *S_PARAM_NAMES[4] = { "s11", "s21", "s12", "s22" };

static int sparam_index(const char *name) {
    for (int i = 0; i < 4; i++) {
        if (strcmp(name, S_PARAM_NAMES[i]) == 0) return i;
    }
    return -1;
}

// --- response helpers ---

// Starts a response object, echoing `req`'s "id" field back verbatim
// (whatever JSON type it was -- PROTOCOL.md just requires correlation, not
// a specific type). `req` may be NULL (e.g. the line didn't parse as JSON
// at all, so there's no id to echo).
static cJSON *begin_resp(const cJSON *req) {
    cJSON *resp = cJSON_CreateObject();
    if (req) {
        const cJSON *id = cJSON_GetObjectItemCaseSensitive(req, "id");
        if (id) cJSON_AddItemToObject(resp, "id", cJSON_Duplicate(id, true));
    }
    return resp;
}

// Prints `resp` into the static line buffer and writes it over USB CDC,
// then frees it. cJSON_PrintPreallocated writes directly into our buffer
// (no malloc'd string to separately free) and reports overflow via its
// return value instead of truncating silently.
static void send_json(cJSON *resp) {
    if (cJSON_PrintPreallocated(resp, resp_buf, (int) sizeof(resp_buf), false)) {
        size_t len = strlen(resp_buf);
        if (len + 1 < sizeof(resp_buf)) resp_buf[len++] = '\n';

        // tud_cdc_write() only copies into TinyUSB's own TX FIFO
        // (CFG_TUD_CDC_TX_BUFSIZE=512, tusb_config.h) -- it does not block,
        // and bytes only actually leave the device inside tud_task(),
        // which main.c's loop calls once per outer iteration. A single
        // short response always fit in one FIFO's worth, so a single
        // write+flush always looked sufficient -- but cmd_sweep()'s
        // per-point loop calls this hundreds of times back-to-back with no
        // yield back to main.c in between, so once the FIFO fills
        // (~7 of these ~70-byte point lines) every further write is
        // silently dropped for the rest of the sweep, even though the
        // firmware's own loop keeps running to completion underneath.
        // Confirmed on real hardware, 2026-10-01: a 1001-point sweep only
        // delivered 6 points over USB, then went silent, while `status`
        // read back "idle" immediately after -- the loop had actually
        // finished, the data just never left the device. Fixed by writing
        // in a loop that pumps tud_task() itself whenever the FIFO has no
        // room, same pattern as any other long/streamed TinyUSB CDC write.
        // Bails out early if the host disconnects mid-write rather than
        // looping forever waiting for a FIFO that will never drain again.
        size_t written = 0;
        uint32_t last_progress = to_ms_since_boot(get_absolute_time());
        while (written < len && tud_cdc_connected()) {
            update_heartbeat();
            uint32_t chunk = tud_cdc_write(resp_buf + written, len - written);
            written += chunk;
            if (chunk > 0) last_progress = to_ms_since_boot(get_absolute_time());
            if (to_ms_since_boot(get_absolute_time()) - last_progress >= 2000) {
                sweep_abort_requested = true;
                break; // Host stopped reading: return so the session can disable RF.
            }
            tud_cdc_write_flush();
            if (written < len) tud_task();
        }
    } // else: overflow -- shouldn't happen at RESP_BUF_SIZE, drop rather than send garbage/truncated JSON
    cJSON_Delete(resp);
}

static void send_ok(const cJSON *req) {
    cJSON *resp = begin_resp(req);
    cJSON_AddBoolToObject(resp, "ok", true);
    send_json(resp);
}

static void send_err(const cJSON *req, const char *error) {
    cJSON *resp = begin_resp(req);
    cJSON_AddBoolToObject(resp, "ok", false);
    cJSON_AddStringToObject(resp, "error", error);
    send_json(resp);
}

// cJSON's own number printing doesn't take a decimal-count argument (it
// picks the shortest round-trip-safe representation) -- rounding the VALUE
// first keeps dB/volts/temperature readings looking like the fixed-decimal
// output this protocol originally had, rather than occasional long float
// tails from division/interpolation results.
static double round_dp(double v, int decimals) {
    double scale = pow(10.0, decimals);
    return round(v * scale) / scale;
}

// cJSON always prints numbers through a double/"%g"-family formatter (see
// print_number() in cJSON.c) -- it has no "this is actually an integer"
// concept. For a large-enough magnitude, %g itself switches to scientific
// notation (e.g. "2.1750400000000e+9") -- syntactically valid JSON, but
// kotlinx.serialization's Long decoder (used client-side for every
// frequency field) rejects it outright: "failed to parse literal ... as a
// long value". Confirmed on real hardware, 2026-10-02, via a real sweep at
// 2.17504GHz. Every frequency field in this protocol is always a plain
// non-negative integer number of Hz (up to 6000000000, the MAX2871's own
// range), so it's formatted here ourselves with plain, non-exponential
// %llu into a raw JSON fragment (cJSON_AddRawToObject bypasses the normal
// double-based formatter entirely) rather than ever going through
// cJSON_AddNumberToObject for one.
static void add_freq_to_object(cJSON *object, const char *name, uint64_t freq_hz) {
    char buf[21]; // up to 20 digits for a uint64_t + NUL
    snprintf(buf, sizeof(buf), "%llu", (unsigned long long) freq_hz);
    cJSON_AddRawToObject(object, name, buf);
}

// Validate JSON numbers before narrowing/casting: out-of-range float-to-int
// conversions are undefined in C. Frequencies and step counts must be integers.
static bool integer_in_range(const cJSON *item, double minimum, double maximum) {
    return cJSON_IsNumber(item) && isfinite(item->valuedouble) &&
           item->valuedouble >= minimum && item->valuedouble <= maximum &&
           floor(item->valuedouble) == item->valuedouble;
}

static bool valid_frequency(const cJSON *item) {
    return integer_in_range(item, (double) SNA_FREQ_MIN_HZ, (double) SNA_FREQ_MAX_HZ);
}

static void add_point_timing(cJSON *object, const rf_point_timing_t *t) {
    cJSON *timing = cJSON_AddObjectToObject(object, "timing_us");
    cJSON_AddNumberToObject(timing, "tune", (double)t->tune_us);
    cJSON_AddNumberToObject(timing, "path", (double)t->path_us);
    cJSON_AddNumberToObject(timing, "settle", (double)t->settle_us);
    cJSON_AddNumberToObject(timing, "lock_wait", (double)t->lock_wait_us);
    cJSON_AddNumberToObject(timing, "lock_acquire", (double)t->lock_acquire_us);
    cJSON_AddNumberToObject(timing, "adc", (double)t->adc_us);
    cJSON_AddNumberToObject(timing, "processing", (double)t->processing_us);
    cJSON_AddNumberToObject(timing, "total", (double)t->total_us);
    cJSON_AddNumberToObject(timing, "adc_reads", t->adc_reads);
}

// --- command handlers ---
// Each takes the whole parsed request object (owned/freed by the caller,
// protocol_handle_line below) rather than a raw line + separately-extracted
// id, since cJSON gives direct field access without re-scanning text.

static void cmd_ping(const cJSON *req) {
    cJSON *resp = begin_resp(req);
    cJSON_AddBoolToObject(resp, "ok", true);
    cJSON_AddStringToObject(resp, "fw", FW_VERSION);
    cJSON_AddNumberToObject(resp, "freq_min_hz", (double) SNA_FREQ_MIN_HZ);
    cJSON_AddNumberToObject(resp, "freq_max_hz", (double) SNA_FREQ_MAX_HZ);
    cJSON_AddBoolToObject(resp, "sweep_atten", true);
    cJSON_AddNumberToObject(resp, "pfd_hz", 100000000);
    cJSON_AddNumberToObject(resp, "cp_current_code", 15);
    cJSON_AddNumberToObject(resp, "adc_samples", RF_DETECTOR_SAMPLES);
    cJSON_AddNumberToObject(resp, "detector_settle_us", RF_DETECTOR_SETTLE_US);
    cJSON_AddBoolToObject(resp, "adc_io_quiet", true);
    cJSON_AddBoolToObject(resp, "sweep_raw_adc", true);
#ifdef SNA_MULTICORE
    cJSON_AddBoolToObject(resp, "multicore", true);
#endif
#ifdef SNA_PLL_CP20_EXPERIMENT
    cJSON_AddBoolToObject(resp, "pll_cp20", true);
#else
    cJSON_AddBoolToObject(resp, "pll_cp20", false);
#endif
    cJSON_AddNumberToObject(resp, "hw_rev", 1);
    cJSON_AddNumberToObject(resp, "proto", PROTO_VERSION);
    send_json(resp);
}

static void cmd_status(const cJSON *req) {
    cJSON *resp = begin_resp(req);
    cJSON_AddBoolToObject(resp, "ok", true);
    cJSON_AddStringToObject(resp, "state", sweeping ? "sweeping" : "idle");
    cJSON_AddNumberToObject(resp, "z", rf_get_z());
    cJSON_AddNumberToObject(resp, "source_port", rf_get_port());
    cJSON_AddNumberToObject(resp, "atten", rf_get_atten());
    cJSON_AddNullToObject(resp, "last_error");
    send_json(resp);
}

static void cmd_set_z(const cJSON *req) {
    const cJSON *z_item = cJSON_GetObjectItemCaseSensitive(req, "z");
    int64_t z = integer_in_range(z_item, 50, 75) ? (int64_t) z_item->valuedouble : -1;
    if (z != 50 && z != 75) {
        send_err(req, "bad_z");
        return;
    }
    rf_set_z((uint8_t) z);
    send_ok(req);
}

static void cmd_abort(const cJSON *req) {
    sweep_abort_requested = true;
    stop_demo_sweep();
    send_ok(req);
}

static void cmd_adc_read(const cJSON *req) {
    uint16_t raw = rf_detector_read_raw();
    cJSON *resp = begin_resp(req);
    cJSON_AddBoolToObject(resp, "ok", true);
    cJSON_AddNumberToObject(resp, "raw", raw);
    cJSON_AddNumberToObject(resp, "volts", round_dp((raw / 4095.0) * 3.3, 3));
    send_json(resp);
}

static void cmd_src_temp_read(const cJSON *req) {
    double temp_c;
    if (!rf_max2871_read_temp_c(&temp_c)) {
        send_err(req, "not_valid");
        return;
    }
    cJSON *resp = begin_resp(req);
    cJSON_AddBoolToObject(resp, "ok", true);
    cJSON_AddNumberToObject(resp, "temp_c", round_dp(temp_c, 1));
    send_json(resp);
}

static void cmd_bootsel(const cJSON *req) {
    send_ok(req);
    sleep_ms(50); // let the response actually leave over USB before we vanish
    reset_usb_boot(0, 0);
}

static void cmd_manual(const cJSON *req) {
    const cJSON *block_item = cJSON_GetObjectItemCaseSensitive(req, "block");
    if (!cJSON_IsString(block_item)) {
        send_err(req, "missing_block");
        return;
    }
    const char *block = block_item->valuestring;
    const cJSON *value_item = cJSON_GetObjectItemCaseSensitive(req, "value");

    if (strcmp(block, "atten") == 0) {
        int64_t ival = integer_in_range(value_item, 0, 127) ? (int64_t) value_item->valuedouble : -1;
        if (ival < 0 || ival > 127) {
            send_err(req, "bad_value");
            return;
        }
        rf_set_atten((uint8_t) ival);
    } else if (strcmp(block, "lpf") == 0) {
        int64_t ival = integer_in_range(value_item, 0, 127) ? (int64_t) value_item->valuedouble : -1;
        if (ival < 0 || ival > 3) {
            send_err(req, "bad_value");
            return;
        }
        rf_set_lpf((uint8_t) ival);
    } else if (strcmp(block, "cplr") == 0) {
        if (!integer_in_range(value_item, 0, 4095)) {
            send_err(req, "bad_value");
            return;
        }
        rf_set_coupler_raw((uint16_t) (int64_t) value_item->valuedouble);
    } else if (strcmp(block, "port") == 0) {
        int64_t ival = integer_in_range(value_item, 0, 127) ? (int64_t) value_item->valuedouble : -1;
        if (ival != 1 && ival != 2) {
            send_err(req, "bad_value");
            return;
        }
        rf_set_port((uint8_t) ival);
    } else if (strcmp(block, "z") == 0) {
        int64_t ival = integer_in_range(value_item, 0, 127) ? (int64_t) value_item->valuedouble : -1;
        if (ival != 50 && ival != 75) {
            send_err(req, "bad_value");
            return;
        }
        rf_set_z((uint8_t) ival);
    } else if (strcmp(block, "src_freq") == 0) {
        if (!valid_frequency(value_item)) {
            send_err(req, "bad_value");
            return;
        }
        if (!rf_max2871_set_freq((uint64_t) value_item->valuedouble)) {
            send_err(req, "bad_value");
            return;
        }
    } else if (strcmp(block, "src_en") == 0) {
        if (!cJSON_IsBool(value_item)) {
            send_err(req, "bad_value");
            return;
        }
        if (cJSON_IsFalse(value_item)) stop_demo_sweep();
        rf_max2871_enable(cJSON_IsTrue(value_item));
    } else if (strcmp(block, "amp_en") == 0) {
        if (!cJSON_IsBool(value_item)) {
            send_err(req, "bad_value");
            return;
        }
        rf_amp_enable(cJSON_IsTrue(value_item));
    } else {
        send_err(req, "unknown_block");
        return;
    }
    send_ok(req);
}

static void cmd_sweep(const cJSON *req) {
    const cJSON *f0_item = cJSON_GetObjectItemCaseSensitive(req, "f0");
    const cJSON *f1_item = cJSON_GetObjectItemCaseSensitive(req, "f1");
    const cJSON *points_item = cJSON_GetObjectItemCaseSensitive(req, "points");
    if (!valid_frequency(f0_item) || !valid_frequency(f1_item) ||
        f0_item->valuedouble > f1_item->valuedouble ||
        !integer_in_range(points_item, 1, 4096)) {
        send_err(req, "bad_args");
        return;
    }
    int64_t f0_i = (int64_t) f0_item->valuedouble;
    int64_t f1_i = (int64_t) f1_item->valuedouble;
    uint32_t points = (uint32_t) points_item->valuedouble;

    const cJSON *log_item = cJSON_GetObjectItemCaseSensitive(req, "log");
    bool log_sweep = cJSON_IsBool(log_item) && cJSON_IsTrue(log_item);

    const cJSON *z_item = cJSON_GetObjectItemCaseSensitive(req, "z");
    int64_t z_i = z_item == NULL ? 50 :
        (integer_in_range(z_item, 50, 75) ? (int64_t) z_item->valuedouble : -1);
    if (z_i != 50 && z_i != 75) {
        send_err(req, "bad_z");
        return;
    }

    // `params` is a proper JSON array of strings now (the old hand-rolled
    // parser couldn't easily extract a string array, so this used to be a
    // substring-search hack over the raw field text).
    bool want_s11 = false, want_s21 = false, want_s12 = false, want_s22 = false;
    const cJSON *params = cJSON_GetObjectItemCaseSensitive(req, "params");
    if (params != NULL && !cJSON_IsArray(params)) {
        send_err(req, "bad_params");
        return;
    }
    if (cJSON_IsArray(params)) {
        const cJSON *p;
        cJSON_ArrayForEach(p, params) {
            if (!cJSON_IsString(p) || sparam_index(p->valuestring) < 0) {
                send_err(req, "bad_params");
                return;
            }
            if (strcmp(p->valuestring, "s11") == 0) want_s11 = true;
            else if (strcmp(p->valuestring, "s21") == 0) want_s21 = true;
            else if (strcmp(p->valuestring, "s12") == 0) want_s12 = true;
            else if (strcmp(p->valuestring, "s22") == 0) want_s22 = true;
        }
    } else {
        want_s11 = want_s21 = true; // PROTOCOL.md's example usage
    }

    if (!want_s11 && !want_s21 && !want_s12 && !want_s22) {
        send_err(req, "bad_params");
        return;
    }
    const cJSON *profile_item = cJSON_GetObjectItemCaseSensitive(req, "profile");
    bool profiling = cJSON_IsTrue(profile_item);
    const cJSON *raw_item = cJSON_GetObjectItemCaseSensitive(req, "raw_adc");
    if (raw_item && !cJSON_IsBool(raw_item)) { send_err(req, "bad_args"); return; }
    bool include_raw = cJSON_IsTrue(raw_item);
    const cJSON *unlocked_item = cJSON_GetObjectItemCaseSensitive(req, "allow_unlocked");
    if (unlocked_item && !cJSON_IsBool(unlocked_item)) { send_err(req, "bad_args"); return; }
    bool allow_unlocked = !cJSON_IsFalse(unlocked_item);
    const cJSON *atten_item = cJSON_GetObjectItemCaseSensitive(req, "atten");
    if (atten_item && !integer_in_range(atten_item, 0, 127)) { send_err(req, "bad_args"); return; }
    const cJSON *timeout_item = cJSON_GetObjectItemCaseSensitive(req, "lock_timeout_ms");
    uint32_t timeout_us = profiling ? 2000000u : 20000u;
    if (profiling && timeout_item) {
        if (!integer_in_range(timeout_item, 20, 10000)) {
            send_err(req, "bad_args");
            return;
        }
        timeout_us = (uint32_t)timeout_item->valuedouble * 1000u;
    }
    rf_set_lock_timeout_us(timeout_us);
#ifndef SNA_MULTICORE
    rf_set_allow_unlocked(allow_unlocked);
#endif
    measurement_requires_usb = true;
    uint64_t sweep_started = time_us_64();
    uint64_t startup_started = sweep_started;
    rf_point_timing_t totals = {0};
    uint64_t transmit_us = 0, max_lock_us = 0, max_lock_frequency = 0;
    uint32_t attempted = 0, valid_points = 0, failed_points = 0, unlocked_points = 0;
#ifdef SNA_MULTICORE
    uint64_t consumer_wait_us = 0;
#endif
    stop_demo_sweep();
#ifndef SNA_MULTICORE
    uint8_t previous_atten = rf_get_atten();
    if (atten_item) rf_set_atten((uint8_t)atten_item->valueint);
#endif
    sweeping = true;
    sweep_abort_requested = false;

    // Enable amp/detector/source ONCE for the whole sweep (including the
    // one-time double-write/lock-wait dance) -- see rf_measure_session_start()
    // for why this matters for a many-point sweep's throughput. frac=0 at
    // i=0 regardless of log/linear, so the first point's frequency is
    // always just f0_i -- no need to duplicate the interpolation formula
    // below just to compute it a second time.
    update_status_leds();
    eeprom_cal_prefetch();
#ifdef SNA_MULTICORE
    acquisition_config_t config = {.f0=(uint64_t)f0_i, .f1=(uint64_t)f1_i,
        .points=points, .timeout_us=timeout_us, .z=(uint8_t)z_i,
        .s11=want_s11, .s21=want_s21, .s12=want_s12, .s22=want_s22,
        .logarithmic=log_sweep, .profiling=profiling, .allow_unlocked=allow_unlocked,
        .override_atten=atten_item != NULL, .atten=atten_item ? (uint8_t)atten_item->valueint : 0};
    uint64_t hardware_startup_us;
    bool measurement_failed = !acquisition_begin(config, &hardware_startup_us);
#else
    bool measurement_failed = !rf_measure_session_start((uint8_t) z_i, (uint64_t) f0_i);
#endif
    uint64_t startup_us = time_us_64() - startup_started;
    bool initial_lock_failed = measurement_failed;
    uint64_t failed_frequency = (uint64_t) f0_i;
    uint32_t failed_point = 0;
    // cmd_sweep() blocks the main loop for its entire duration (this whole
    // function is one long synchronous call), so main()'s normal once-per-
    // iteration update_status_leds() call never runs until this returns --
    // without calling it here too, the BUSY LED (and the live port/
    // impedance LEDs) would never reflect reality during a real sweep.
    // Confirmed on real hardware, 2026-10-03. Call it immediately so BUSY
    // lights up right away, not just after the first point's settle time.
    update_status_leds();

    for (uint32_t i = 0; i < points && !initial_lock_failed; i++) {
        tud_task();
        poll_usb_commands();
        if (!tud_cdc_connected()) sweep_abort_requested = true;
        if (sweep_abort_requested) break;

        double frac = (points > 1) ? ((double) i / (double) (points - 1)) : 0.0;
        uint64_t f;
        if (i == 0) f = (uint64_t) f0_i;
        else if (i == points - 1) f = (uint64_t) f1_i;
        else if (log_sweep) {
            double lf = log((double) f0_i) + frac * (log((double) f1_i) - log((double) f0_i));
            f = (uint64_t) exp(lf);
        } else {
            f = (uint64_t) ((double) f0_i + frac * (double) (f1_i - f0_i));
        }

#ifdef SNA_MULTICORE
        acquisition_result_t acquired;
        uint64_t receive_started = time_us_64();
        if (!acquisition_next(&acquired)) break;
        consumer_wait_us += time_us_64() - receive_started;
        f = acquired.frequency;
        rf_raw_measurement_t raw = acquired.raw;
        rf_point_timing_t timing = acquired.timing;
        uint64_t processing_started = time_us_64();
        rf_measurement_t m = rf_process_point(raw, f, (uint8_t)z_i,
                                            want_s11, want_s21, want_s12, want_s22);
        uint64_t processing_us = time_us_64() - processing_started;
        timing.processing_us += processing_us;
        timing.total_us += processing_us;
#else
        rf_raw_measurement_t raw = rf_acquire_point(f, (uint8_t)z_i, want_s11, want_s21, want_s12, want_s22);
        rf_point_timing_t timing = rf_get_point_timing();
        uint64_t processing_started = time_us_64();
        rf_measurement_t m = rf_process_point(raw, f, (uint8_t)z_i, want_s11, want_s21, want_s12, want_s22);
        uint64_t processing_us = time_us_64() - processing_started;
        timing.processing_us += processing_us;
        timing.total_us += processing_us;
#endif
        attempted++;
        if (m.valid) valid_points++; else failed_points++;
        totals.tune_us += timing.tune_us; totals.path_us += timing.path_us;
        totals.settle_us += timing.settle_us; totals.lock_wait_us += timing.lock_wait_us;
        totals.lock_acquire_us += timing.lock_acquire_us; totals.adc_us += timing.adc_us;
        totals.processing_us += timing.processing_us; totals.total_us += timing.total_us;
        totals.adc_reads += timing.adc_reads;
        if (timing.lock_acquire_us > max_lock_us) {
            max_lock_us = timing.lock_acquire_us;
            max_lock_frequency = f;
        }
        if (protocol_measurement_cancelled()) break;
        if (m.measured && !m.valid) unlocked_points++;
        if (!m.measured) {
            measurement_failed = true;
            failed_frequency = f;
            failed_point = i;
            if (!profiling) break;
        }

        cJSON *resp = begin_resp(req);
        cJSON_AddNumberToObject(resp, "pt", i);
        add_freq_to_object(resp, "f", f);
        if (m.measured && want_s11) cJSON_AddNumberToObject(resp, "s11_db", round_dp(m.s11_db, 2));
        if (m.measured && want_s21) cJSON_AddNumberToObject(resp, "s21_db", round_dp(m.s21_db, 2));
        if (m.measured && want_s12) cJSON_AddNumberToObject(resp, "s12_db", round_dp(m.s12_db, 2));
        if (m.measured && want_s22) cJSON_AddNumberToObject(resp, "s22_db", round_dp(m.s22_db, 2));
        cJSON_AddBoolToObject(resp, "locked", m.valid);
        cJSON_AddBoolToObject(resp, "measured", m.measured);
        if (!m.calibrated) cJSON_AddBoolToObject(resp, "cal", false);
        if (include_raw && m.measured) {
            cJSON *adc = cJSON_AddObjectToObject(resp, "raw_adc");
            if (want_s11 || want_s21) cJSON_AddNumberToObject(adc, "source1_ref", raw.source1_ref);
            if (want_s11) cJSON_AddNumberToObject(adc, "source1_reflection", raw.source1_reflection);
            if (want_s21) cJSON_AddNumberToObject(adc, "source1_transmission", raw.source1_transmission);
            if (want_s12 || want_s22) cJSON_AddNumberToObject(adc, "source2_ref", raw.source2_ref);
            if (want_s22) cJSON_AddNumberToObject(adc, "source2_reflection", raw.source2_reflection);
            if (want_s12) cJSON_AddNumberToObject(adc, "source2_transmission", raw.source2_transmission);
        }
        if (profiling) {
            cJSON_AddBoolToObject(resp, "valid", m.valid);
            add_point_timing(resp, &timing);
        }
        uint64_t transmit_started = time_us_64();
        send_json(resp);
        transmit_us += time_us_64() - transmit_started;
        update_status_leds(); // keep BUSY/PORT1_OUT_EN/PORT_Z50 live during the sweep -- see above
    }

    uint64_t shutdown_started = time_us_64();
#ifdef SNA_MULTICORE
    uint64_t hardware_shutdown_us = acquisition_end();
#else
    rf_measure_session_end();
#endif
    uint64_t shutdown_us = time_us_64() - shutdown_started;
    sweeping = false;
    measurement_requires_usb = false;
    rf_set_lock_timeout_us(20000u);
#ifndef SNA_MULTICORE
    rf_set_allow_unlocked(false);
    if (atten_item) rf_set_atten(previous_atten);
#endif
    update_status_leds();

    cJSON *resp = begin_resp(req);
    cJSON_AddBoolToObject(resp, "ok", !sweep_abort_requested && !measurement_failed);
    cJSON_AddBoolToObject(resp, "done", true);
    if (unlocked_points) {
        cJSON_AddStringToObject(resp, "warning", "source_unlocked");
        cJSON_AddNumberToObject(resp, "unlocked_points", unlocked_points);
    }
    if (sweep_abort_requested) cJSON_AddStringToObject(resp, "error", "aborted");
    else if (measurement_failed) {
        cJSON_AddStringToObject(resp, "error", "source_not_locked");
        cJSON_AddStringToObject(resp, "stage", initial_lock_failed ? "startup" : "retune");
        cJSON_AddNumberToObject(resp, "pt", failed_point);
        add_freq_to_object(resp, "f", failed_frequency);
    }
    if (profiling) {
        cJSON *profile = cJSON_AddObjectToObject(resp, "profile");
#ifdef SNA_MULTICORE
        cJSON_AddBoolToObject(profile, "multicore", true);
        cJSON_AddNumberToObject(profile, "hardware_startup_us", (double)hardware_startup_us);
        cJSON_AddNumberToObject(profile, "hardware_shutdown_us", (double)hardware_shutdown_us);
        acquisition_stats_t stats = acquisition_stats();
        cJSON_AddNumberToObject(profile, "producer_acquired", stats.acquired);
        cJSON_AddNumberToObject(profile, "producer_hardware_us", (double)stats.hardware_us);
        cJSON_AddNumberToObject(profile, "producer_queue_wait_us", (double)stats.queue_wait_us);
        cJSON_AddNumberToObject(profile, "consumer_wait_us", (double)consumer_wait_us);
#endif
        cJSON_AddNumberToObject(profile, "attempted", attempted);
        cJSON_AddNumberToObject(profile, "valid", valid_points);
        cJSON_AddNumberToObject(profile, "failed", failed_points);
        cJSON_AddNumberToObject(profile, "lock_timeout_us", timeout_us);
        cJSON_AddNumberToObject(profile, "startup_us", (double)startup_us);
        cJSON_AddNumberToObject(profile, "shutdown_us", (double)shutdown_us);
        cJSON_AddNumberToObject(profile, "transmit_us", (double)transmit_us);
        cJSON_AddNumberToObject(profile, "wall_us", (double)(time_us_64() - sweep_started));
        cJSON_AddNumberToObject(profile, "max_lock_acquire_us", (double)max_lock_us);
        add_freq_to_object(profile, "max_lock_f", max_lock_frequency);
        add_point_timing(profile, &totals);
    }
    send_json(resp);
}

static void cmd_cal_set(const cJSON *req) {
    const cJSON *set_item = cJSON_GetObjectItemCaseSensitive(req, "set");
    if (!cJSON_IsString(set_item)) {
        send_err(req, "missing_set");
        return;
    }
    int idx = cal_set_index(set_item->valuestring);
    if (idx < 0) {
        send_err(req, "unknown_set");
        return;
    }

    const cJSON *f0_item = cJSON_GetObjectItemCaseSensitive(req, "f0");
    const cJSON *f1_item = cJSON_GetObjectItemCaseSensitive(req, "f1");
    if (!valid_frequency(f0_item) || !valid_frequency(f1_item) ||
        f0_item->valuedouble > f1_item->valuedouble) {
        send_err(req, "missing_grid");
        return;
    }

    const cJSON *points_arr = cJSON_GetObjectItemCaseSensitive(req, "points");
    if (!cJSON_IsArray(points_arr)) {
        send_err(req, "bad_points");
        return;
    }
    int n = cJSON_GetArraySize(points_arr);
    if (n <= 0 || n > EEPROM_CAL_POINTS_MAX) {
        send_err(req, "bad_points");
        return;
    }

    static double points[EEPROM_CAL_POINTS_MAX];
    for (int i = 0; i < n; i++) {
        const cJSON *v = cJSON_GetArrayItem(points_arr, i);
        if (!cJSON_IsNumber(v) || !isfinite(v->valuedouble) ||
            v->valuedouble < -327.68 || v->valuedouble > 327.67) {
            send_err(req, "bad_points");
            return;
        }
        points[i] = v->valuedouble;
    }

    // uint64_t, not uint32_t -- f1 can be up to 6,000,000,000 Hz, which
    // overflows a uint32_t (see eeprom_cal_grid_t's comment in eeprom.c for
    // the real-hardware corruption this used to cause).
    if (!eeprom_cal_set(idx, (uint64_t) f0_item->valuedouble, (uint64_t) f1_item->valuedouble,
                         points, (size_t) n)) {
        send_err(req, "grid_mismatch");
        return;
    }
    send_ok(req);
}

static void cmd_cal_get(const cJSON *req) {
    const cJSON *set_item = cJSON_GetObjectItemCaseSensitive(req, "set");
    if (!cJSON_IsString(set_item)) {
        send_err(req, "missing_set");
        return;
    }
    int idx = cal_set_index(set_item->valuestring);
    if (idx < 0) {
        send_err(req, "unknown_set");
        return;
    }

    static double points[EEPROM_CAL_POINTS_MAX];
    uint64_t f0, f1;
    size_t count;
    if (!eeprom_cal_get(idx, &f0, &f1, points, &count)) {
        send_err(req, "not_calibrated");
        return;
    }

    cJSON *resp = begin_resp(req);
    cJSON_AddBoolToObject(resp, "ok", true);
    cJSON_AddStringToObject(resp, "set", set_item->valuestring);
    add_freq_to_object(resp, "f0", f0);
    add_freq_to_object(resp, "f1", f1);
    cJSON *arr = cJSON_AddArrayToObject(resp, "points");
    for (size_t i = 0; i < count; i++) cJSON_AddItemToArray(arr, cJSON_CreateNumber(round_dp(points[i], 2)));
    send_json(resp);
}

static void cmd_cal_clear(const cJSON *req) {
    const cJSON *set_item = cJSON_GetObjectItemCaseSensitive(req, "set");
    if (!cJSON_IsString(set_item)) {
        send_err(req, "missing_set");
        return;
    }
    bool stored;
    if (strcmp(set_item->valuestring, "all") == 0) {
        stored = eeprom_cal_clear_all();
    } else {
        int idx = cal_set_index(set_item->valuestring);
        if (idx < 0) {
            send_err(req, "unknown_set");
            return;
        }
        stored = eeprom_cal_clear(idx);
    }
    if (!stored) send_err(req, "storage_error");
    else send_ok(req);
}

static void cmd_sdi_test_set(const cJSON *req) {
    const cJSON *points_arr = cJSON_GetObjectItemCaseSensitive(req, "points");
    if (!cJSON_IsArray(points_arr)) {
        send_err(req, "bad_points");
        return;
    }
    int n = cJSON_GetArraySize(points_arr);
    if (n > EEPROM_SDI_POINTS_MAX) {
        send_err(req, "bad_points");
        return;
    }

    static sdi_test_point_t points[EEPROM_SDI_POINTS_MAX];
    int count = 0;
    const cJSON *elem;
    cJSON_ArrayForEach(elem, points_arr) {
        const cJSON *f = cJSON_GetObjectItemCaseSensitive(elem, "f");
        const cJSON *thresh = cJSON_GetObjectItemCaseSensitive(elem, "thresh_db");
        const cJSON *cmp = cJSON_GetObjectItemCaseSensitive(elem, "cmp");
        const cJSON *s_param = cJSON_GetObjectItemCaseSensitive(elem, "s_param");
        // SDI format 2 still stores u32 Hz: reject unsupported values instead of wrapping.
        if (!integer_in_range(f, (double) SNA_FREQ_MIN_HZ, (double) UINT32_MAX) ||
            !cJSON_IsNumber(thresh) || !isfinite(thresh->valuedouble) ||
            thresh->valuedouble < -327.68 || thresh->valuedouble > 327.67 ||
            !cJSON_IsString(cmp) || !cJSON_IsString(s_param) ||
            (strcmp(cmp->valuestring, "gt") != 0 && strcmp(cmp->valuestring, "lt") != 0)) {
            send_err(req, "bad_points");
            return;
        }
        int sparam_idx = sparam_index(s_param->valuestring);
        if (sparam_idx < 0) {
            send_err(req, "bad_points");
            return;
        }
        points[count].freq_hz = (uint32_t) f->valuedouble;
        points[count].thresh_db_x100 = (int16_t) lround(thresh->valuedouble * 100.0);
        points[count].cmp_gt = (strcmp(cmp->valuestring, "gt") == 0);
        points[count].s_param = (uint8_t) sparam_idx;
        count++;
    }
    if (!eeprom_sdi_test_set(points, (size_t) count)) send_err(req, "storage_error");
    else send_ok(req);
}

static void cmd_sdi_test_get(const cJSON *req) {
    static sdi_test_point_t points[EEPROM_SDI_POINTS_MAX];
    size_t count;
    eeprom_sdi_test_get(points, &count);

    cJSON *resp = begin_resp(req);
    cJSON_AddBoolToObject(resp, "ok", true);
    cJSON *arr = cJSON_AddArrayToObject(resp, "points");
    for (size_t i = 0; i < count; i++) {
        cJSON *pt = cJSON_CreateObject();
        add_freq_to_object(pt, "f", points[i].freq_hz);
        cJSON_AddNumberToObject(pt, "thresh_db", round_dp(points[i].thresh_db_x100 / 100.0, 2));
        cJSON_AddStringToObject(pt, "cmp", points[i].cmp_gt ? "gt" : "lt");
        cJSON_AddStringToObject(pt, "s_param", S_PARAM_NAMES[points[i].s_param & 0x3]);
        cJSON_AddItemToArray(arr, pt);
    }
    send_json(resp);
}

// --- dispatch ---

void protocol_init(void) {
    sweeping = false;
    sweep_abort_requested = false;
}

void protocol_handle_line(const char *line) {
    cJSON *req = cJSON_Parse(line);
    if (!req) {
        send_err(NULL, "missing_cmd"); // malformed JSON -- no "cmd" to find either way
        return;
    }

    const cJSON *cmd_item = cJSON_GetObjectItemCaseSensitive(req, "cmd");
    if (!cJSON_IsString(cmd_item)) {
        send_err(req, "missing_cmd");
        cJSON_Delete(req);
        return;
    }
    const char *cmd = cmd_item->valuestring;
    if (sweeping && strcmp(cmd, "abort") != 0 && strcmp(cmd, "status") != 0 &&
        strcmp(cmd, "ping") != 0) {
        send_err(req, "busy");
        cJSON_Delete(req);
        return;
    }

    if (strcmp(cmd, "ping") == 0) cmd_ping(req);
    else if (strcmp(cmd, "status") == 0) cmd_status(req);
    else if (strcmp(cmd, "set_z") == 0) cmd_set_z(req);
    else if (strcmp(cmd, "sweep") == 0) cmd_sweep(req);
    else if (strcmp(cmd, "abort") == 0) cmd_abort(req);
    else if (strcmp(cmd, "cal_set") == 0) cmd_cal_set(req);
    else if (strcmp(cmd, "cal_get") == 0) cmd_cal_get(req);
    else if (strcmp(cmd, "cal_clear") == 0) cmd_cal_clear(req);
    else if (strcmp(cmd, "sdi_test_set") == 0) cmd_sdi_test_set(req);
    else if (strcmp(cmd, "sdi_test_get") == 0) cmd_sdi_test_get(req);
    else if (strcmp(cmd, "manual") == 0) cmd_manual(req);
    else if (strcmp(cmd, "adc_read") == 0) cmd_adc_read(req);
    else if (strcmp(cmd, "src_temp_read") == 0) cmd_src_temp_read(req);
    else if (strcmp(cmd, "bootsel") == 0) cmd_bootsel(req);
    else send_err(req, "unknown_cmd");

    cJSON_Delete(req);
}

void protocol_run_sdi_self_test(void) {
    static sdi_test_point_t points[EEPROM_SDI_POINTS_MAX];
    size_t count;
    eeprom_sdi_test_get(points, &count);
    if (count == 0) return;

    bool overall_pass = true;
    bool results_pass[EEPROM_SDI_POINTS_MAX];
    double results_val[EEPROM_SDI_POINTS_MAX];

    stop_demo_sweep();
    measurement_requires_usb = false;
    sweeping = true;
    sweep_abort_requested = false;
    update_status_leds();
#ifdef SNA_MULTICORE
    uint64_t frequencies[EEPROM_SDI_POINTS_MAX];
    uint8_t masks[EEPROM_SDI_POINTS_MAX];
    for (size_t i=0; i<count; i++) {
        frequencies[i] = points[i].freq_hz;
        masks[i] = (uint8_t)(1u << points[i].s_param);
    }
    eeprom_cal_prefetch();
    acquisition_config_t config = {.f0=frequencies[0], .points=(uint32_t)count,
        .timeout_us=20000, .z=rf_get_z(), .profiling=true,
        .frequencies=frequencies, .masks=masks};
    uint64_t startup_us;
    bool session_ok = acquisition_begin(config, &startup_us);
#else
    bool session_ok = rf_measure_session_start(rf_get_z(), points[0].freq_hz);
#endif
    for (size_t i = 0; i < count; i++) {
        tud_task();
        poll_usb_commands();
        // Which S-parameter this point evaluates is now configurable per-point
        // (points[i].s_param, uploaded via sdi_test_set's "s_param" field) --
        // previously hardcoded to S11/return-loss here, per the user's
        // explicit request, 2026-10-02.
        uint8_t sp = points[i].s_param & 0x3;
        rf_measurement_t m = {0};
        if (session_ok && !sweep_abort_requested) {
#ifdef SNA_MULTICORE
            acquisition_result_t r;
            if (acquisition_next(&r))
                m = rf_process_point(r.raw, r.frequency, rf_get_z(),
                                     sp == 0, sp == 1, sp == 2, sp == 3);
#else
            m = rf_measure_point(points[i].freq_hz, rf_get_z(),
                                 sp == 0, sp == 1, sp == 2, sp == 3);
#endif
        }
        double val;
        switch (sp) {
            case 1: val = m.s21_db; break;
            case 2: val = m.s12_db; break;
            case 3: val = m.s22_db; break;
            default: val = m.s11_db; break;
        }
        double thresh = points[i].thresh_db_x100 / 100.0;
        bool pass = m.valid && (points[i].cmp_gt ? (val > thresh) : (val < thresh));
        results_pass[i] = pass;
        results_val[i] = val;
        if (!pass) overall_pass = false;
    }
#ifdef SNA_MULTICORE
    acquisition_end();
#else
    rf_measure_session_end();
#endif
    // Blink out the sequence: one green/red blink per point in order, then
    // a pause. The button handler itself isn't re-entrant/looping here --
    // a caller wanting continuous repeat would need to re-invoke this on a
    // timer, which main.c doesn't currently do (runs once per button press).
    for (size_t i = 0; i < count; i++) {
        gpio_put(GPIO_SDI_TEST_PASS_LED, results_pass[i] ? 1 : 0);
        gpio_put(GPIO_SDI_TEST_FAIL_LED, results_pass[i] ? 0 : 1);
        for (unsigned ms = 0; ms < 300 && !sweep_abort_requested; ++ms) {
            tud_task();
            poll_usb_commands();
            sleep_ms(1);
        }
        gpio_put(GPIO_SDI_TEST_PASS_LED, 0);
        gpio_put(GPIO_SDI_TEST_FAIL_LED, 0);
        for (unsigned ms = 0; ms < 150 && !sweep_abort_requested; ++ms) {
            tud_task();
            poll_usb_commands();
            sleep_ms(1);
        }
        if (sweep_abort_requested) break;
    }
    sweeping = false;
    update_status_leds();

    if (tud_cdc_connected()) {
        cJSON *evt = cJSON_CreateObject();
        cJSON_AddStringToObject(evt, "event", "sdi_test_result");
        cJSON_AddBoolToObject(evt, "pass", overall_pass);
        cJSON *results = cJSON_AddArrayToObject(evt, "results");
        for (size_t i = 0; i < count; i++) {
            cJSON *r = cJSON_CreateObject();
            add_freq_to_object(r, "f", points[i].freq_hz);
            cJSON_AddNumberToObject(r, "val_db", round_dp(results_val[i], 2));
            cJSON_AddBoolToObject(r, "pass", results_pass[i]);
            cJSON_AddItemToArray(results, r);
        }
        send_json(evt);
    }
}
