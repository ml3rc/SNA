// Compile the real main/protocol/RF/EEPROM code with a fake USB link and chip HALs.
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "sna.h"
#include "main.h"
#include "protocol.h"
#include "rf_control.h"
#include "hal/rf_switch.h"
#include "hal/eeprom_24aa32a.h"
#include "cJSON.h"

static bool pins[48], locked = true, source_enabled, amp_enabled, detector_enabled;
static bool connected = true, stalled, write_failure;
static uint64_t now_us;
static uint64_t last_tune_us;
static uint64_t last_switch_us;
static uint32_t lock_delay_us;
static bool lock_bounce;
static uint64_t first_adc_us;
static uint8_t storage[4096];
static char output[200000], input[20000];
static size_t output_len, input_pos, input_len;
static unsigned reads, port_changes;
static unsigned gpio_writes;
static unsigned read_source[8], read_path[8];
static unsigned input_polls;
static int disconnect_after = -1;
static bool inject_during_display;
static unsigned heartbeat_writes;

void gpio_init(uint pin) { pins[pin] = false; }
void gpio_set_dir(uint pin, bool out) { (void)pin; (void)out; }
void gpio_put(uint pin, bool value) {
    pins[pin] = value; ++gpio_writes;
    if (pin == GPIO_LED_BOOTED) ++heartbeat_writes;
}
bool gpio_get(uint pin) { return pins[pin]; }
void gpio_pull_up(uint pin) { (void)pin; }
void gpio_set_drive_strength(uint pin, enum gpio_drive_strength drive) { (void)pin; (void)drive; }
void sleep_us(uint64_t us) { now_us += us; }
void sleep_ms(uint32_t ms) {
    now_us += (uint64_t)ms * 1000;
    if (inject_during_display && ms == 1 && !source_enabled) {
        inject_during_display = false;
        strcpy(input, "{\"cmd\":\"ping\",\"id\":81}\n{\"cmd\":\"manual\",\"id\":82,\"block\":\"port\",\"value\":2}\n");
        input_pos = 0; input_len = strlen(input);
    }
}
absolute_time_t get_absolute_time(void) { return now_us; }
uint64_t time_us_64(void) { return now_us; }
uint32_t to_ms_since_boot(absolute_time_t t) { return (uint32_t)(t / 1000); }
void reset_usb_boot(uint32_t mask, uint32_t disable) { (void)mask; (void)disable; }
void tusb_init(void) {}
bool tud_disconnect(void) { return true; }
bool tud_connect(void) { return true; }
void tud_task(void) {
    now_us += 100;
    if (disconnect_after >= 0 && (int)input_polls++ >= disconnect_after) connected = false;
}
bool tud_cdc_connected(void) { return connected; }
uint32_t tud_cdc_available(void) { return (uint32_t)(input_len - input_pos); }
uint32_t tud_cdc_read(void *buf, uint32_t count) {
    size_t left = input_len - input_pos;
    if (count > left) count = (uint32_t)left;
    memcpy(buf, input + input_pos, count);
    input_pos += count;
    return count;
}
uint32_t tud_cdc_write(const void *buf, uint32_t count) {
    if (stalled) return 0;
    assert(output_len + count < sizeof(output));
    memcpy(output + output_len, buf, count);
    output_len += count;
    output[output_len] = 0;
    return count;
}
void tud_cdc_write_flush(void) {}
void eeprom_24aa32a_init(void) {}
bool eeprom_24aa32a_read(uint16_t addr, uint8_t *buf, size_t count) {
    assert(!(protocol_is_sweeping() && source_enabled && reads > 0));
    assert((size_t)addr + count <= sizeof(storage));
    memcpy(buf, storage + addr, count); return true;
}
bool eeprom_24aa32a_write(uint16_t addr, const uint8_t *buf, size_t count) {
    assert((size_t)addr + count <= sizeof(storage));
    if (write_failure) return false;
    memcpy(storage + addr, buf, count); return true;
}
void rf_switch_init(const rf_switch_t *sw) { (void)sw; }
void rf_switch_set(const rf_switch_t *sw, uint8_t code) {
    now_us += 2;
    if (sw->gpio_c1 == GPIO_SW_PORT_C1) ++port_changes;
    pins[sw->gpio_c1] = code & 1;
    pins[sw->gpio_c2] = (code >> 1) & 1;
    last_switch_us = now_us;
}
void rf_switch_set_drive_strength(const rf_switch_t *sw, enum gpio_drive_strength drive) { (void)sw; (void)drive; }
void hmc1119_init(void) {}
void hmc1119_set_atten(uint8_t value) { (void)value; }
void trf37a73_init(void) {}
void trf37a73_enable(bool en) { amp_enabled = en; }
void ad8318_init(void) {}
void ad8318_enable(bool en) { detector_enabled = en; }
static unsigned selected_path(void) {
    const uint c1[] = {3,5,7,9,11,13}, c2[] = {2,4,6,8,10,12};
    // Independent copy of the board-confirmed switch matrix.
    const uint8_t codes[4][6] = {{2,0,3,2,0,1},{0,0,0,1,2,1},{1,2,1,3,3,2},{1,1,2,0,3,0}};
    for (unsigned path = 0; path < 4; ++path) {
        bool match = true;
        for (unsigned i = 0; i < 6; ++i) {
            if ((unsigned)(pins[c1[i]] | pins[c2[i]] << 1) != codes[path][i]) match = false;
        }
        if (match) return path;
    }
    assert(!"unrecognized coupler state"); return 0;
}
uint16_t ad8318_read_raw(void) {
    assert(source_enabled && amp_enabled && detector_enabled);
    if (protocol_is_sweeping()) {
        // Model core 0 refreshing LEDs while core 1 samples: zero writes.
        unsigned before = gpio_writes;
        update_status_leds();
        assert(gpio_writes == before);
    }
    unsigned source = rf_get_port(), path = selected_path();
    assert(pins[GPIO_LED_PORT1_OUT_EN] == (source == 1));
    assert((pins[GPIO_SW_PORT_C1] | pins[GPIO_SW_PORT_C2] << 1) == (source == 1 ? 2 : 1));
    if (reads == 0) first_adc_us = now_us;
    unsigned batch = reads / RF_DETECTOR_SAMPLES;
    unsigned sample = reads % RF_DETECTOR_SAMPLES;
    assert(now_us - last_switch_us >= RF_DETECTOR_SETTLE_US);
    if (batch < 8) { read_source[batch] = source; read_path[batch] = path; }
    ++reads;
    now_us += 3;
    // A nonreciprocal DUT: S11=-10, S21=-20, S12=-30, S22=-40 dB.
    // Unused paths have unrelated values, so incorrect excitation cannot accidentally pass.
    const double volts[2][4] = {{1.0,1.245,0.8,1.49},{0.6,1.935,1.2,2.18}};
    // Three low and three high spikes per batch, deliberately unsorted.
    // They must not bias the median, regardless of source or detector path.
    if (sample == 0 || sample == 4 || sample == 11) return 0;
    if (sample == 2 || sample == 8 || sample == 15) return 4095;
    return (uint16_t)lround(volts[source - 1][path] * 4095.0 / 3.3);
}
void max2871_init(void) {}
void max2871_enable(bool en) { source_enabled = en; }
bool max2871_set_freq(uint64_t f) {
    now_us += 400;
    last_tune_us = now_us;
    return f >= 50000000ULL && f <= 6000000000ULL;
}
bool max2871_locked(void) {
    uint64_t elapsed = now_us - last_tune_us;
    if (lock_bounce && elapsed >= 130 && elapsed < 180) return false;
    return locked && elapsed >= lock_delay_us;
}
uint8_t max2871_read_die_id(void) { return 7; }
bool max2871_read_temp_c(double *out) { *out = 25.0; return true; }

static void reset_io(const char *lines) {
    connected = true; stalled = false; disconnect_after = -1; input_polls = 0;
    output_len = 0; output[0] = 0; input_pos = 0;
    input_len = strlen(lines); assert(input_len < sizeof(input));
    memcpy(input, lines, input_len + 1);
    reads = 0; port_changes = 0;
}
static void assert_rf_off(void) {
    assert(!source_enabled && !amp_enabled && !detector_enabled);
    assert(!protocol_is_sweeping());
    assert(!pins[GPIO_LED_BUSY] && pins[GPIO_LED_READY]);
}

int main(void) {
    memset(storage, 0xff, sizeof(storage)); eeprom_init(); protocol_init();
    reset_io("");
    assert(!rf_measure_session_start(50, 49999999ULL));
    assert(!rf_max2871_set_freq(49999999ULL));
    assert(!rf_acquire_point(49999999ULL, 50, true, false, false, false).measured);
    assert(reads == 0);
    assert(!source_enabled && !amp_enabled && !detector_enabled);
    rf_set_z(50);
    for (unsigned mask = 1; mask < 16; ++mask) {
        bool s11 = mask & 1, s21 = mask & 2, s12 = mask & 4, s22 = mask & 8;
        reset_io("");
        assert(rf_measure_session_start(50, 5000000000ULL));
        rf_measurement_t m = rf_measure_point(5000000000ULL, 50, s11, s21, s12, s22);
        assert(m.valid && !m.calibrated);
        if (s11) assert(fabs(m.s11_db + 10) < 0.04);
        if (s21) assert(fabs(m.s21_db + 20) < 0.04);
        if (s12) assert(fabs(m.s12_db + 30) < 0.04);
        if (s22) assert(fabs(m.s22_db + 40) < 0.04);
        unsigned pos = 0;
        if (s11 || s21) {
            assert(read_source[pos] == 1 && read_path[pos++] == RF_COUPLER_PORT1_FWD);
            if (s11) { assert(read_source[pos] == 1 && read_path[pos] == RF_COUPLER_PORT1_BWD); ++pos; }
            if (s21) { assert(read_source[pos] == 1 && read_path[pos] == RF_COUPLER_PORT2_BWD); ++pos; }
        }
        if (s12 || s22) {
            assert(read_source[pos] == 2 && read_path[pos++] == RF_COUPLER_PORT2_FWD);
            if (s22) { assert(read_source[pos] == 2 && read_path[pos] == RF_COUPLER_PORT2_BWD); ++pos; }
            if (s12) { assert(read_source[pos] == 2 && read_path[pos] == RF_COUPLER_PORT1_BWD); ++pos; }
        }
        assert(reads == pos * RF_DETECTOR_SAMPLES);
        assert(port_changes == (unsigned)((s11 || s21) && (s12 || s22) ? 2 : 1));
        rf_measure_session_end();
    }
    puts("PASS: all 15 S-parameter combinations, ADC spike rejection, settling, source GPIO and live port LED");
    reset_io("");
    assert(rf_measure_session_start(50, 5000000000ULL));
    lock_delay_us = 3000; // Regression: a healthy 3 ms retune failed the former 1 ms deadline.
    uint64_t before = now_us;
    rf_measurement_t delayed = rf_measure_point(5100000000ULL, 50, true, false, false, false);
    assert(delayed.valid && now_us - before >= 3100 && now_us - before < 20000);
    lock_delay_us = 25000;
    delayed = rf_measure_point(5200000000ULL, 50, true, false, false, false);
    assert(!delayed.valid);
    lock_delay_us = 0;
    reset_io(""); lock_bounce = true;
    delayed = rf_measure_point(5300000000ULL, 50, true, false, false, false);
    assert(delayed.valid && first_adc_us - last_tune_us >= 300);
    lock_bounce = false;
    rf_measure_session_end();
    puts("PASS: delayed lock succeeds, brief LD drop resets stability check, excessive delay fails");
    const double correction[] = {5.0};
    for (int idx = 0; idx < 4; ++idx) assert(eeprom_cal_set(idx, 50000000ULL, 6000000000ULL, correction, 1));
    reset_io("");
    assert(rf_measure_session_start(50, 5000000000ULL));
    rf_measurement_t calibrated = rf_measure_point(5000000000ULL, 50, true, true, true, true);
    assert(calibrated.valid && calibrated.calibrated);
    assert(fabs(calibrated.s11_db + 15) < 0.04 && fabs(calibrated.s21_db + 25) < 0.04);
    assert(fabs(calibrated.s12_db + 35) < 0.04 && fabs(calibrated.s22_db + 45) < 0.04);
    rf_measure_session_end();
    assert(eeprom_cal_clear_all());
    const char *sweep = "{\"cmd\":\"sweep\",\"allow_unlocked\":false,\"id\":1,\"f0\":50000000,\"f1\":6000000000,\"points\":3,\"params\":[\"s21\"]}";
    reset_io(""); protocol_handle_line(sweep);
    assert(strstr(output, "\"f\":6000000000")); assert(strstr(output, "\"done\":true")); assert(reads == 6 * RF_DETECTOR_SAMPLES); assert_rf_off();
    assert(!strstr(output, "\"raw_adc\""));
    reset_io("");
    protocol_handle_line("{\"cmd\":\"sweep\",\"id\":1,\"f0\":100000000,\"f1\":100000000,\"points\":1,\"params\":[\"s22\"],\"raw_adc\":true}");
    cJSON *raw_record = cJSON_Parse(output); assert(raw_record);
    cJSON *adc_record = cJSON_GetObjectItemCaseSensitive(raw_record, "raw_adc"); assert(adc_record);
    assert(cJSON_GetObjectItemCaseSensitive(adc_record, "source2_ref")->valueint == (int)lround(1.2 * 4095 / 3.3));
    assert(cJSON_GetObjectItemCaseSensitive(adc_record, "source2_reflection")->valueint == (int)lround(2.18 * 4095 / 3.3));
    assert(!cJSON_GetObjectItemCaseSensitive(adc_record, "source1_ref"));
    assert(!cJSON_GetObjectItemCaseSensitive(adc_record, "source2_transmission"));
    assert(fabs(cJSON_GetObjectItemCaseSensitive(raw_record, "s22_db")->valuedouble + 40) < 0.04);
    cJSON_Delete(raw_record); assert_rf_off();
    reset_io("{\"cmd\":\"abort\",\"id\":2}\n"); protocol_handle_line(sweep);
    assert(strstr(output, "\"error\":\"aborted\"")); assert(reads == 0); assert_rf_off();
    char command_batch[1024];
    snprintf(command_batch, sizeof(command_batch), "%s\n{\"cmd\":\"status\",\"id\":8}\n{\"cmd\":\"abort\",\"id\":9}\n", sweep);
    reset_io(command_batch); poll_usb_commands();
    assert(strstr(output, "\"state\":\"sweeping\"")); assert(strstr(output, "\"source_port\":"));
    assert(strstr(output, "\"id\":1,\"ok\":false,\"done\":true,\"error\":\"aborted\""));
    assert(reads == 0); assert_rf_off();
    reset_io("{\"cmd\":\"manual\",\"id\":3,\"block\":\"port\",\"value\":2}\n"); protocol_handle_line(sweep);
    assert(strstr(output, "\"error\":\"busy\"")); assert(reads == 6 * RF_DETECTOR_SAMPLES); assert_rf_off();
    reset_io(""); disconnect_after = 0; protocol_handle_line(sweep); assert(reads == 0); assert_rf_off();
    reset_io(""); stalled = true; protocol_handle_line(sweep); assert(reads == 2 * RF_DETECTOR_SAMPLES); assert_rf_off();
    reset_io(""); locked = false; protocol_handle_line(sweep);
    assert(strstr(output, "source_not_locked")); assert(strstr(output, "\"stage\":\"startup\""));
    assert(strstr(output, "\"f\":50000000")); assert(reads == 0); assert_rf_off(); locked = true;
    reset_io(""); lock_delay_us = 25000; protocol_handle_line(sweep);
    assert(strstr(output, "\"stage\":\"retune\"")); assert(reads == 0); assert_rf_off(); lock_delay_us = 0;
    const char *profile_sweep = "{\"cmd\":\"sweep\",\"allow_unlocked\":false,\"id\":1,\"f0\":50000000,\"f1\":6000000000,\"points\":3,\"params\":[\"s21\"],\"profile\":true}";
    reset_io(""); lock_delay_us = 25000; protocol_handle_line(profile_sweep);
    assert(reads == 6 * RF_DETECTOR_SAMPLES && strstr(output, "\"profile\":{") && strstr(output, "\"failed\":0"));
    char *line_end = strchr(output, '\n'); assert(line_end); *line_end = 0;
    cJSON *record = cJSON_Parse(output); assert(record);
    cJSON *timing = cJSON_GetObjectItemCaseSensitive(record, "timing_us"); assert(timing);
    assert(cJSON_GetObjectItemCaseSensitive(timing, "tune")->valuedouble == 400);
    assert(cJSON_GetObjectItemCaseSensitive(timing, "path")->valuedouble > 0);
    assert(cJSON_GetObjectItemCaseSensitive(timing, "adc")->valuedouble == 6 * RF_DETECTOR_SAMPLES);
    assert(cJSON_GetObjectItemCaseSensitive(timing, "adc_reads")->valueint == 2 * RF_DETECTOR_SAMPLES);
    assert(cJSON_GetObjectItemCaseSensitive(timing, "lock_acquire")->valuedouble >= 25100);
    double sum = 0;
    const char *exclusive[] = {"tune","path","settle","lock_wait","adc","processing"};
    for (size_t i = 0; i < count_of(exclusive); i++) sum += cJSON_GetObjectItemCaseSensitive(timing, exclusive[i])->valuedouble;
    assert(sum == cJSON_GetObjectItemCaseSensitive(timing, "total")->valuedouble);
    cJSON_Delete(record);
    assert_rf_off();
    reset_io(""); protocol_handle_line(sweep); // profiling timeout must not leak into normal sweeps
    assert(reads == 0 && strstr(output, "source_not_locked")); assert_rf_off();
    reset_io(""); protocol_handle_line("{\"cmd\":\"sweep\",\"allow_unlocked\":false,\"id\":1,\"f0\":50000000,\"f1\":6000000000,\"points\":3,\"params\":[\"s21\"],\"profile\":true,\"lock_timeout_ms\":20}");
    assert(reads == 0 && strstr(output, "\"attempted\":3") && strstr(output, "\"failed\":3"));
    assert(!strstr(output, "s21_db")); assert_rf_off();
    reset_io("{\"cmd\":\"abort\",\"id\":7}\n"); protocol_handle_line(profile_sweep);
    assert(strstr(output, "\"error\":\"aborted\"")); assert(reads == 0); assert_rf_off();
    lock_delay_us = 0;
    puts("PASS: full-sweep profiling, phase accounting, timed-out points, timeout restoration and Abort during lock wait");
    puts("PASS: sweep endpoints, Abort, busy-command isolation, disconnect, stalled host and lock failure");
    reset_io(""); locked = false;
    protocol_handle_line("{\"cmd\":\"sweep\",\"id\":1,\"f0\":50000000,\"f1\":6000000000,\"points\":3,\"params\":[\"s21\"]}");
    assert(reads == 6 * RF_DETECTOR_SAMPLES && strstr(output, "\"s21_db\""));
    assert(strstr(output, "\"locked\":false") && strstr(output, "\"measured\":true"));
    assert(strstr(output, "\"warning\":\"source_unlocked\"") && strstr(output, "\"unlocked_points\":3"));
    assert(!strstr(output, "\"error\":\"source_not_locked\""));
    assert_rf_off(); locked = true;
    reset_io(""); lock_delay_us = 25000;
    protocol_handle_line("{\"cmd\":\"sweep\",\"id\":1,\"f0\":50000000,\"f1\":6000000000,\"points\":3,\"params\":[\"s21\"]}");
    assert(reads == 6 * RF_DETECTOR_SAMPLES && strstr(output, "\"unlocked_points\":3")); assert_rf_off();
    reset_io("{\"cmd\":\"abort\",\"id\":7}\n");
    protocol_handle_line("{\"cmd\":\"sweep\",\"id\":1,\"f0\":50000000,\"f1\":6000000000,\"points\":3,\"params\":[\"s21\"]}");
    assert(reads == 0 && strstr(output, "\"error\":\"aborted\"")); assert_rf_off(); lock_delay_us=0;
    puts("PASS: unlocked fallback returns real detector values, continues sweep, flags confidence and respects Abort");
    rf_set_atten(64); reset_io("");
    protocol_handle_line("{\"cmd\":\"sweep\",\"id\":1,\"f0\":50000000,\"f1\":6000000000,\"points\":3,\"params\":[\"s21\"],\"atten\":0}");
    assert(reads==6 * RF_DETECTOR_SAMPLES && rf_get_atten()==64); assert_rf_off();
    reset_io(""); locked=false;
    protocol_handle_line("{\"cmd\":\"sweep\",\"id\":1,\"f0\":50000000,\"f1\":6000000000,\"points\":3,\"params\":[\"s21\"],\"atten\":0,\"allow_unlocked\":false}");
    assert(reads==0 && rf_get_atten()==64); assert_rf_off(); locked=true;
    reset_io("{\"cmd\":\"abort\",\"id\":7}\n");
    protocol_handle_line("{\"cmd\":\"sweep\",\"id\":1,\"f0\":50000000,\"f1\":6000000000,\"points\":3,\"params\":[\"s21\"],\"atten\":0}");
    assert(reads==0 && rf_get_atten()==64); assert_rf_off();
    puts("PASS: temporary calibration attenuation is restored after success, failed startup and Abort");
    const char *bad[] = {
        "{\"cmd\":\"sweep\",\"f0\":100000000,\"f1\":100000000,\"points\":1,\"raw_adc\":1}",
        "{\"cmd\":\"sweep\",\"f0\":49999999,\"f1\":6000000000,\"points\":3}",
        "{\"cmd\":\"manual\",\"block\":\"src_freq\",\"value\":49999999}",
        "{\"cmd\":\"cal_set\",\"set\":\"s11_50\",\"f0\":49999999,\"f1\":6000000000,\"points\":[0]}",
        "{\"cmd\":\"sdi_test_set\",\"points\":[{\"f\":49999999,\"thresh_db\":-10,\"s_param\":\"s11\",\"cmp\":\"lt\"}]}",
        "{\"cmd\":\"sweep\",\"f0\":50000000,\"f1\":6000000000,\"points\":3,\"atten\":128}",
        "{\"cmd\":\"sweep\",\"allow_unlocked\":false,\"f0\":-1,\"f1\":6000000000,\"points\":3}",
        "{\"cmd\":\"sweep\",\"allow_unlocked\":false,\"f0\":6000000000,\"f1\":50000000,\"points\":3}",
        "{\"cmd\":\"sweep\",\"allow_unlocked\":false,\"f0\":50000000,\"f1\":6000000000,\"points\":1.5}",
        "{\"cmd\":\"sweep\",\"allow_unlocked\":false,\"f0\":50000000,\"f1\":6000000000,\"points\":3,\"z\":51}",
        "{\"cmd\":\"sweep\",\"allow_unlocked\":false,\"f0\":50000000,\"f1\":6000000000,\"points\":3,\"params\":[]}",
        "{\"cmd\":\"sweep\",\"allow_unlocked\":false,\"f0\":50000000,\"f1\":6000000000,\"points\":3,\"params\":[\"bad\"]}",
        "{\"cmd\":\"manual\",\"block\":\"src_freq\",\"value\":1e100}",
        "{\"cmd\":\"sdi_test_set\",\"points\":[{\"f\":6000000000,\"thresh_db\":-3,\"cmp\":\"lt\",\"s_param\":\"s11\"}]}",
        "{\"cmd\":\"sdi_test_set\",\"points\":[{\"f\":100000000,\"thresh_db\":-3,\"cmp\":\"typo\",\"s_param\":\"s11\"}]}",
        "{\"cmd\":\"cal_set\",\"set\":\"s11_50\",\"f0\":50000000,\"f1\":6000000000,\"points\":[1000]}"
    };
    for (size_t i = 0; i < count_of(bad); ++i) {
        reset_io(""); protocol_handle_line(bad[i]); assert(strstr(output, "\"ok\":false")); assert(reads == 0); assert_rf_off();
    }
    reset_io(""); write_failure = true; protocol_handle_line("{\"cmd\":\"cal_clear\",\"set\":\"all\"}");
    assert(strstr(output, "storage_error")); write_failure = false;
    // Standalone SDI works without CDC and cannot pass on an invalid measurement.
    sdi_test_point_t threshold = {.freq_hz = 100000000, .thresh_db_x100 = -500, .s_param = 0};
    assert(eeprom_sdi_test_set(&threshold, 1));
    reset_io(""); connected = false; protocol_run_sdi_self_test(); assert(reads == 2 * RF_DETECTOR_SAMPLES); assert_rf_off();
    reset_io(""); locked = false; protocol_run_sdi_self_test();
    assert(strstr(output, "\"pass\":false")); assert(reads == 0); assert_rf_off(); locked = true;
    sdi_test_point_t display_points[3] = {threshold, threshold, threshold};
    assert(eeprom_sdi_test_set(display_points, 3));
    reset_io(""); inject_during_display = true;
    unsigned heartbeat_before = heartbeat_writes;
    protocol_run_sdi_self_test();
    assert(!inject_during_display);
    assert(strstr(output, "\"id\":81,\"ok\":true"));
    assert(strstr(output, "\"id\":82,\"ok\":false,\"error\":\"busy\""));
    assert(heartbeat_writes >= heartbeat_before + 2);
    assert(!protocol_is_sweeping()); assert_rf_off();
    puts("PASS: SDI LED display services USB, rejects RF mutations and keeps heartbeat running");
    // Oversized input must discard its tail, even if that tail is itself a valid command.
    reset_io(""); memset(input, 'x', 9000);
    strcpy(input + 9000, "{\"cmd\":\"ping\",\"id\":99}\n{\"cmd\":\"ping\",\"id\":100}\n");
    input_len = strlen(input); poll_usb_commands(); poll_usb_commands();
    assert(!strstr(output, "\"id\":99")); assert(strstr(output, "\"id\":100"));
    puts("PASS: invalid commands, storage errors and oversized-line resynchronization");
    return 0;
}
