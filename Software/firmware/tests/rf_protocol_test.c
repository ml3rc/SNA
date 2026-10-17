// Compile the real main/protocol/rf_task/RF/EEPROM code with a fake USB link
// and chip HALs. Both cores run on this one thread: run() alternates core 1's
// rf_task_step() with core 0's line reader and protocol_poll() until nothing
// is left to do, which keeps every scenario deterministic.
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "hardware/gpio.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "sna.h"
#include "tusb.h"
#include "eeprom.h"
#include "main.h"
#include "measurement.h"
#include "protocol.h"
#include "rf_task.h"
#include "sdi_test.h"
#include "rf_control/rf_control.h"
#include "rf_control/state.h" // the test also drives the state layer directly
#include "hal/rf_switch.h"
#include "hal/eeprom_24aa32a.h"
#include "cJSON.h"

static bool pins[48], locked = true, source_enabled, amp_enabled, detector_enabled;
static bool output_terminated(void) { return rf_state_get_source() != RF_STATE_SOURCE_ON; }
static bool gen_active(void) { return rf_app_generator_on(); }
static bool connected = true, stalled, write_failure, rebooted;
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

unsigned int get_core_num(void) { return 0; }
void multicore_launch_core1(void (*entry)(void)) { (void)entry; } // run() is core 1
void gpio_init(uint pin) { pins[pin] = false; }
void gpio_set_dir(uint pin, bool out) { (void)pin; (void)out; }
void gpio_put(uint pin, bool value) { pins[pin] = value; ++gpio_writes; }
void gpio_set_function(uint pin, uint function) { (void)pin; (void)function; }
static uint16_t pwm_level[64]; // per pin; 1000 = always on
unsigned int pwm_gpio_to_slice_num(uint gpio) { return (gpio >> 1) & 7; }
void pwm_set_wrap(uint slice, uint16_t wrap) { (void)slice; assert(wrap == 999); }
void pwm_set_clkdiv(uint slice, float divider) { (void)slice; (void)divider; }
void pwm_set_enabled(uint slice, bool enabled) { (void)slice; (void)enabled; }
void pwm_set_gpio_level(uint gpio, uint16_t level) { assert(level <= 1000); pwm_level[gpio] = level; }
bool gpio_get(uint pin) { return pins[pin]; }
void gpio_pull_up(uint pin) { (void)pin; }
void gpio_set_drive_strength(uint pin, enum gpio_drive_strength drive) { (void)pin; (void)drive; }
void sleep_us(uint64_t us) { now_us += us; }
void sleep_ms(uint32_t ms) { now_us += (uint64_t)ms * 1000; }
absolute_time_t get_absolute_time(void) { return now_us; }
uint64_t time_us_64(void) { return now_us; }
uint32_t to_ms_since_boot(absolute_time_t t) { return (uint32_t)(t / 1000); }
void reset_usb_boot(uint32_t mask, uint32_t disable) { (void)mask; (void)disable; rebooted = true; }
bool tusb_init(void) { return true; }
bool add_repeating_timer_ms(int32_t delay_ms, repeating_timer_callback_t callback, void *user_data, repeating_timer_t *out) {
    (void)delay_ms; (void)callback; (void)user_data; (void)out; return true;
}
bool cancel_repeating_timer(repeating_timer_t *timer) { (void)timer; return true; }
bool tud_disconnect(void) { return true; }
bool tud_connect(void) { return true; }
void tud_task(void) { now_us += 100; }
bool tud_task_event_ready(void) { return false; }
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
uint32_t tud_cdc_write_available(void) { return stalled ? 0 : 512; }
static unsigned tx_clears, rx_flushes;
bool tud_cdc_write_clear(void) { ++tx_clears; return true; }
void tud_cdc_read_flush(void) { ++rx_flushes; input_pos = input_len; }
// Core 1's loop never runs here (run() steps it), so the watchdog is never fed after boot; it is a no-op.
void watchdog_enable(uint32_t delay_ms, bool pause_on_debug) { (void)delay_ms; (void)pause_on_debug; }
void watchdog_update(void) {}
void hal_eeprom_24aa32a_init(void) {}
bool hal_eeprom_24aa32a_read(uint16_t addr, uint8_t *buf, size_t count) {
    // No I2C while a measurement samples the detector: calibration is prefetched.
    assert(!(protocol_is_sweeping() && rf_app_source_on()));
    assert((size_t)addr + count <= sizeof(storage));
    memcpy(buf, storage + addr, count); return true;
}
bool hal_eeprom_24aa32a_write(uint16_t addr, const uint8_t *buf, size_t count) {
    assert((size_t)addr + count <= sizeof(storage));
    if (write_failure) return false;
    memcpy(storage + addr, buf, count); return true;
}
void hal_rf_switch_init(const hal_rf_switch_t *sw) { (void)sw; }
void hal_rf_switch_set(const hal_rf_switch_t *sw, uint8_t code) {
    now_us += 2;
    if (sw->gpio_c1 == GPIO_SW_PORT_C1) ++port_changes;
    pins[sw->gpio_c1] = code & 1;
    pins[sw->gpio_c2] = (code >> 1) & 1;
    last_switch_us = now_us;
}
void hal_hmc1119_init(void) {}
static uint8_t hw_atten = 127;
void hal_hmc1119_set_atten(uint8_t value) { hw_atten = value; }
void hal_trf37a73_init(void) {}
void hal_trf37a73_enable(bool en) { amp_enabled = en; }
void hal_ad8318_init(void) {}
void hal_ad8318_enable(bool en) { detector_enabled = en; }
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
static unsigned parked_read_path = 99;
static uint16_t parked_read_value = 1500;
uint16_t hal_ad8318_read_raw(void) {
    if (output_terminated()) { // power meter: source terminated, only the coupler path matters
        assert(detector_enabled);
        parked_read_path = selected_path();
        ++reads;
        return parked_read_value;
    }
    assert(source_enabled && amp_enabled && detector_enabled);
    assert(hw_atten == rf_app_get_atten()); // requested code reaches the hardware once the source is on
    if (protocol_is_sweeping()) {
        // Model core 0 refreshing LEDs while core 1 samples: zero writes.
        unsigned before = gpio_writes;
        update_status_leds();
        assert(gpio_writes == before);
    }
    unsigned source = rf_app_get_port(), path = selected_path();
    assert(pins[GPIO_LED_PORT1_OUT_EN] == (source == 1));
    assert((pins[GPIO_SW_PORT_C1] | pins[GPIO_SW_PORT_C2] << 1) == (source == 1 ? 2 : 1));
    if (reads == 0) first_adc_us = now_us;
    unsigned batch = reads / RF_CTRL_DETECTOR_SAMPLES;
    unsigned sample = reads % RF_CTRL_DETECTOR_SAMPLES;
    assert(now_us - last_switch_us >= RF_CTRL_DETECTOR_SETTLE_US);
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
void hal_max2871_init(void) {}
void hal_max2871_enable(bool en) { source_enabled = en; }
static unsigned freq_writes;
static uint64_t freq_write_gap_us; // between the starts of the last two writes
bool hal_max2871_set_freq(uint64_t f) {
    static uint64_t prev_write_us;
    ++freq_writes; freq_write_gap_us = now_us - prev_write_us; prev_write_us = now_us;
    now_us += 400;
    last_tune_us = now_us;
    return f >= 50000000ULL && f <= 6000000000ULL;
}
bool hal_max2871_locked(void) {
    uint64_t elapsed = now_us - last_tune_us;
    if (lock_bounce && elapsed >= 130 && elapsed < 180) return false;
    return locked && elapsed >= lock_delay_us;
}
bool hal_max2871_read_temp_c(double *out) { *out = 25.0; return true; }

static void reset_io(const char *lines) {
    connected = true; stalled = false;
    output_len = 0; output[0] = 0; input_pos = 0;
    input_len = strlen(lines); assert(input_len < sizeof(input));
    memcpy(input, lines, input_len + 1);
    reads = 0; port_changes = 0;
}
// Both cores until neither has anything left: the host's lines, core 1's
// requests and sweep points, core 0's replies and events.
static void run(void) {
    for (unsigned guard = 0; guard < 1000000; ++guard) {
        poll_usb_commands();
        bool work = rf_task_step();
        work |= protocol_poll();
        if (!work && input_pos == input_len) return;
    }
    assert(!"run() did not settle");
}
static void cmd(const char *line) { reset_io(""); protocol_handle_line(line); run(); }
static void batch(const char *lines) { reset_io(lines); run(); }
static bool has(const char *text) { return strstr(output, text) != NULL; }

static void assert_rf_off(void) {
    // The port switch is parked (C1=C2=0) with the hardware attenuator at 127;
    // the MAX2871, amp and detector deliberately keep running.
    assert(rf_state_get_source() == RF_STATE_SOURCE_PARKED && source_enabled && amp_enabled && detector_enabled);
    assert(output_terminated() && !pins[GPIO_SW_PORT_C1] && !pins[GPIO_SW_PORT_C2]);
    assert(hw_atten == 127);
    assert(!pins[GPIO_LED_PORT1_OUT_EN]);
    assert(!protocol_is_sweeping() && !rf_app_sweep_active());
}

// Mode changes through the protocol, the way a host does them.
static void enter_mode(const char *name) {
    char line[96];
    snprintf(line, sizeof(line), "{\"cmd\":\"mode_enter\",\"id\":90,\"mode\":\"%s\"}", name);
    cmd(line);
    assert(has("\"ok\":true"));
}
static void exit_mode(void) {
    cmd("{\"cmd\":\"mode_exit\",\"id\":91}");
    assert(has("\"ok\":true,\"mode\":\"idle\"") && rf_app_get_mode() == RF_APP_IDLE);
}
static cJSON *first_line(void) {
    char *nl = strchr(output, '\n'); assert(nl); *nl = 0;
    cJSON *json = cJSON_Parse(output); assert(json);
    *nl = '\n';
    return json;
}
static uint32_t now_ms(void) { return (uint32_t)(now_us / 1000); }

static bool never(void) { return false; }
static const rf_app_hooks_t never_cancelled = { .cancelled = never };

#define SWEEP_STRICT "{\"cmd\":\"sweep\",\"allow_unlocked\":false,\"id\":1,\"f0\":50000000,\"f1\":6000000000,\"points\":3,\"z\":50,\"params\":[\"s21\"],\"atten\":0}"
static const double correction[] = {5.0}; // dB, stored flat across the band

#define SWEEP_DEFAULT "{\"cmd\":\"sweep\",\"id\":1,\"f0\":50000000,\"f1\":6000000000,\"points\":3,\"z\":50,\"params\":[\"s21\"],\"atten\":0}"

static void test_boot(void) {
    memset(storage, 0xff, sizeof(storage)); assert(eeprom_init());
    reset_io("");
    assert(!rf_app_sweep_begin(50, 49999999ULL, false, 0));
    assert(!rf_state_set_freq(49999999ULL));
    assert(!rf_app_sweep_point(49999999ULL, true, false, false, false).measured);
    assert(reads == 0);
    assert(!detector_enabled); // rejected before the session touched anything
    // Boot self-test: no lock and a dead detector are both reported, the
    // hardware still ends up parked.
    locked = false; parked_read_value = 0;
    assert(rf_app_init(&never_cancelled) == (RF_APP_INIT_ERR_NOT_LOCKED | RF_APP_INIT_ERR_DETECTOR));
    assert(rf_state_get_source() == RF_STATE_SOURCE_PARKED && hw_atten == 127);
    // Without hooks init fails: parked, no self-test, and nothing long-running can start.
    locked = true; reset_io("");
    assert(rf_app_init(NULL) == RF_APP_INIT_ERR_NO_HOOKS && reads == 0);
    assert(rf_app_init(&(rf_app_hooks_t){0}) == RF_APP_INIT_ERR_NO_HOOKS);
    assert(rf_state_get_source() == RF_STATE_SOURCE_PARKED && hw_atten == 127);
    assert(rf_app_mode_enter(RF_APP_SWEEP) == RF_APP_OK && !rf_app_sweep_begin(50, 1000000000ULL, true, 0));
    rf_app_sweep_end(); assert(rf_app_mode_exit() == RF_APP_OK);
    assert(rf_app_mode_enter(RF_APP_GENERATOR) == RF_APP_OK);
    assert(rf_app_generator_start(1, 50, 0, 1000000000ULL) == RF_APP_ERR_NOT_LOCKED && output_terminated());
    assert(rf_app_mode_exit() == RF_APP_OK);
    // The real boot: core 1 initializes the RF side for the rf_task link.
    locked = true; parked_read_value = 2621; // the board's parked reading
    freq_writes = 0;
    rf_task_start();
    rf_task_core1_init();
    assert(rf_task_wait_ready() == 0);
    puts("PASS: boot self-test reports PLL lock and detector failures");
    parked_read_value = 1500;
    // Boot state: detector, source and amp running, output parked (terminated,
    // attenuator 127) -- the source is kept running so its temperature stays stable.
    // Datasheet power-up double write: exactly once per power-up, >=20 ms apart,
    // whichever path switched the source on; plain retunes are a single write.
    assert(freq_writes == 2 && freq_write_gap_us >= 20000);
    assert(detector_enabled && source_enabled && amp_enabled);
    assert(rf_state_get_source() == RF_STATE_SOURCE_PARKED && hw_atten == 127 && !pins[GPIO_LED_PORT1_OUT_EN]);
    assert(rf_app_get_z() == 50 && pins[GPIO_LED_PORT_Z50] && rf_app_get_port() == 1);
    assert(rf_app_get_mode() == RF_APP_IDLE);
}

// Application layer sweeps, driven directly.
static void test_app_sweeps(void) {
    assert(rf_app_mode_enter(RF_APP_SWEEP) == RF_APP_OK);
    assert(rf_state_set_freq(1000000000ULL) && freq_writes == 3);
    assert(rf_state_set_freq(1100000000ULL) && freq_writes == 4);
    rf_state_set_source(RF_STATE_SOURCE_PARKED); // already running: not a power-up
    assert(rf_state_set_freq(1200000000ULL) && freq_writes == 5);
    assert(rf_state_set_source(RF_STATE_SOURCE_OFF)); assert(!source_enabled && !amp_enabled);
    // Never straight from OFF to ON: refused, nothing switched or powered.
    unsigned changes_before = port_changes;
    assert(!rf_state_set_source(RF_STATE_SOURCE_ON) && rf_state_get_source() == RF_STATE_SOURCE_OFF);
    assert(!source_enabled && !amp_enabled && hw_atten == 127 && port_changes == changes_before);
    assert(!rf_state_set_source(RF_STATE_SOURCE_NONE) && rf_state_get_source() == RF_STATE_SOURCE_OFF);
    assert(rf_state_set_source(RF_STATE_SOURCE_PARKED)); freq_writes = 0; // e.g. manual src_en, then a sweep
    assert(rf_state_set_source(RF_STATE_SOURCE_PARKED)); // already there: fine, nothing to do
    assert(rf_app_sweep_begin(50, 1000000000ULL, false, 0) && freq_writes == 2 && freq_write_gap_us >= 20000);
    rf_app_sweep_end(); freq_writes = 0;
    assert(rf_app_sweep_begin(50, 1000000000ULL, false, 0) && freq_writes == 1); // source left running between sweeps
    rf_app_sweep_end(); reset_io("");
    eeprom_cal_prefetch();
    for (unsigned mask = 1; mask < 16; ++mask) {
        bool s11 = mask & 1, s21 = mask & 2, s12 = mask & 4, s22 = mask & 8;
        reset_io("");
        assert(rf_app_sweep_begin(50, 5000000000ULL, false, 0));
        rf_app_raw_measurement_t raw = rf_app_sweep_point(5000000000ULL, s11, s21, s12, s22);
        measurement_t m = measurement_process(&raw, 5000000000ULL, 50, (uint8_t)mask);
        assert(m.locked && !m.calibrated);
        if (s11) assert(fabs(m.db[0] + 10) < 0.04);
        if (s21) assert(fabs(m.db[1] + 20) < 0.04);
        if (s12) assert(fabs(m.db[2] + 30) < 0.04);
        if (s22) assert(fabs(m.db[3] + 40) < 0.04);
        unsigned pos = 0;
        if (s11 || s21) {
            assert(read_source[pos] == 1 && read_path[pos++] == RF_CTRL_COUPLER_PORT1_FWD);
            if (s11) { assert(read_source[pos] == 1 && read_path[pos] == RF_CTRL_COUPLER_PORT1_BWD); ++pos; }
            if (s21) { assert(read_source[pos] == 1 && read_path[pos] == RF_CTRL_COUPLER_PORT2_BWD); ++pos; }
        }
        if (s12 || s22) {
            assert(read_source[pos] == 2 && read_path[pos++] == RF_CTRL_COUPLER_PORT2_FWD);
            if (s22) { assert(read_source[pos] == 2 && read_path[pos] == RF_CTRL_COUPLER_PORT2_BWD); ++pos; }
            if (s12) { assert(read_source[pos] == 2 && read_path[pos] == RF_CTRL_COUPLER_PORT1_BWD); ++pos; }
        }
        assert(reads == pos * RF_CTRL_DETECTOR_SAMPLES);
        // Only the routing: the source is already PARKED when the session starts, and
        // rf_state_set_source() only touches the hardware when the state changes.
        assert(rf_state_get_source() == RF_STATE_SOURCE_ON);
        assert(port_changes == (unsigned)((s11 || s21) && (s12 || s22) ? 2 : 1));
        rf_app_sweep_end();
    }
    puts("PASS: all 15 S-parameter combinations, ADC spike rejection, settling, source GPIO and live port LED");
    reset_io("");
    assert(rf_app_sweep_begin(50, 5000000000ULL, false, 0));
    lock_delay_us = 3000; // Regression: a healthy 3 ms retune failed the former 1 ms deadline.
    uint64_t before = now_us;
    rf_app_raw_measurement_t delayed = rf_app_sweep_point(5100000000ULL, true, false, false, false);
    assert(delayed.valid && now_us - before >= 3100 && now_us - before < 20000);
    lock_delay_us = 25000;
    delayed = rf_app_sweep_point(5200000000ULL, true, false, false, false);
    assert(!delayed.valid);
    lock_delay_us = 0;
    reset_io(""); lock_bounce = true;
    delayed = rf_app_sweep_point(5300000000ULL, true, false, false, false);
    assert(delayed.valid && first_adc_us - last_tune_us >= 300);
    lock_bounce = false;
    rf_app_sweep_end();
    puts("PASS: delayed lock succeeds, brief LD drop resets stability check, excessive delay fails");
    for (int idx = 0; idx < 4; ++idx) assert(eeprom_cal_set(idx, 50000000ULL, 6000000000ULL, correction, 1) == EEPROM_CAL_OK);
    eeprom_cal_prefetch();
    reset_io("");
    assert(rf_app_sweep_begin(50, 5000000000ULL, false, 0));
    rf_app_raw_measurement_t raw = rf_app_sweep_point(5000000000ULL, true, true, true, true);
    measurement_t calibrated = measurement_process(&raw, 5000000000ULL, 50, 0xF);
    assert(calibrated.locked && calibrated.calibrated);
    assert(fabs(calibrated.db[0] + 15) < 0.04 && fabs(calibrated.db[1] + 25) < 0.04);
    assert(fabs(calibrated.db[2] + 35) < 0.04 && fabs(calibrated.db[3] + 45) < 0.04);
    measurement_t at_75 = measurement_process(&raw, 5000000000ULL, 75, 0xF); // no 75 ohm curves
    assert(!at_75.calibrated && fabs(at_75.db[0] + 10) < 0.04);
    rf_app_sweep_end();
    assert(eeprom_cal_clear_all());
}

// Sweeps through the protocol.
static void test_protocol_sweeps(void) {
    cmd(SWEEP_STRICT);
    assert(has("\"f\":6000000000") && has("\"done\":true") && has("\"locked\":true") && !has("measured"));
    assert(reads == 6 * RF_CTRL_DETECTOR_SAMPLES); assert_rf_off();
    batch(SWEEP_STRICT "\n{\"cmd\":\"abort\",\"id\":2}\n");
    assert(has("\"error\":\"aborted\"") && has("\"id\":2,\"ok\":true")); assert(reads == 0); assert_rf_off();
    // Status and temperature during a sweep: answered between points / from cache.
    batch(SWEEP_STRICT "\n{\"cmd\":\"status\",\"id\":8}\n{\"cmd\":\"src_temp_read\",\"id\":9}\n");
    assert(has("\"id\":8,\"ok\":true,\"mode\":\"sweep\",\"sweeping\":true") && has("\"port\":"));
    assert(has("\"id\":9,\"ok\":false,\"error\":\"not_valid\"")); // no live reading yet
    assert(has("\"id\":1,\"ok\":true,\"done\":true")); assert_rf_off();
    batch(SWEEP_STRICT "\n{\"cmd\":\"manual\",\"id\":3,\"block\":\"port\",\"value\":2}\n");
    assert(has("\"id\":3,\"ok\":false,\"error\":\"busy\"")); assert(reads == 6 * RF_CTRL_DETECTOR_SAMPLES); assert_rf_off();
    // The settings stay available during a sweep: applied between two points.
    batch(SWEEP_STRICT "\n{\"cmd\":\"set\",\"id\":4,\"atten\":20}\n{\"cmd\":\"set\",\"id\":5,\"z\":75}\n");
    assert(has("\"id\":4,\"ok\":true,\"z\":50,\"atten\":20") && has("\"id\":5,\"ok\":true,\"z\":75,\"atten\":20"));
    assert(has("\"done\":true") && rf_app_get_atten() == 20 && rf_app_get_z() == 75); assert_rf_off();
    rf_app_set_z(50);
    // mode_exit ends the sweep first, then the mode is left.
    batch(SWEEP_STRICT "\n{\"cmd\":\"mode_exit\",\"id\":6}\n");
    char *aborted = strstr(output, "\"error\":\"aborted\""), *left = strstr(output, "\"id\":6,\"ok\":true,\"mode\":\"idle\"");
    assert(aborted && left && aborted < left);
    assert(rf_app_get_mode() == RF_APP_IDLE && !protocol_is_sweeping()); assert_rf_off();
    enter_mode("sweep");
    // A vanished host ends the sweep and leaves the mode.
    reset_io(""); connected = false; protocol_handle_line(SWEEP_STRICT); run();
    assert(reads == 0 && rf_app_get_mode() == RF_APP_IDLE); assert_rf_off();
    enter_mode("sweep");
    // A host that stops reading ends the sweep after the stall timeout.
    reset_io(""); stalled = true; protocol_handle_line(SWEEP_STRICT); run();
    assert(reads == 2 * RF_CTRL_DETECTOR_SAMPLES); assert_rf_off();
    locked = false; cmd(SWEEP_STRICT);
    assert(has("source_not_locked") && has("\"stage\":\"startup\"") && has("\"f\":50000000"));
    assert(reads == 0); assert_rf_off(); locked = true;
    lock_delay_us = 25000; cmd(SWEEP_STRICT);
    assert(has("\"stage\":\"retune\"") && has("\"pt\":0")); assert(reads == 0); assert_rf_off(); lock_delay_us = 0;
    puts("PASS: sweep endpoints, abort, status between points, busy commands, settings mid-sweep, mode exit, disconnect, stalled host and lock failure");
    locked = false; cmd(SWEEP_DEFAULT);
    assert(reads == 6 * RF_CTRL_DETECTOR_SAMPLES && has("\"s21_db\"") && has("\"locked\":false"));
    assert(has("\"warning\":\"source_unlocked\"") && has("\"unlocked_points\":3") && !has("\"error\""));
    assert_rf_off(); locked = true;
    lock_delay_us = 25000; cmd(SWEEP_DEFAULT);
    assert(reads == 6 * RF_CTRL_DETECTOR_SAMPLES && has("\"unlocked_points\":3")); assert_rf_off();
    batch(SWEEP_DEFAULT "\n{\"cmd\":\"abort\",\"id\":7}\n");
    assert(reads == 0 && has("\"error\":\"aborted\"")); assert_rf_off(); lock_delay_us = 0;
    puts("PASS: unlocked fallback returns real detector values, continues sweep, flags confidence and respects abort");
    // Every sweep brings its attenuation; it is applied to the points with the source on and stays the setting.
    rf_app_set_atten(64);
    cmd("{\"cmd\":\"sweep\",\"id\":1,\"f0\":50000000,\"f1\":6000000000,\"points\":3,\"z\":50,\"params\":[\"s21\"],\"atten\":12}");
    assert(reads == 6 * RF_CTRL_DETECTOR_SAMPLES && rf_app_get_atten() == 12); assert_rf_off();
    locked = false; cmd(SWEEP_STRICT);
    assert(reads == 0 && rf_app_get_atten() == 0); assert_rf_off(); locked = true;
    // Without it (or z, or params) the sweep is refused before anything is touched.
    rf_app_set_atten(64);
    cmd("{\"cmd\":\"sweep\",\"id\":1,\"f0\":50000000,\"f1\":6000000000,\"points\":3,\"z\":50,\"params\":[\"s21\"]}");
    assert(has("\"error\":\"bad_args\",\"field\":\"atten\"") && reads == 0 && rf_app_get_atten() == 64); assert_rf_off();
    cmd("{\"cmd\":\"sweep\",\"id\":1,\"f0\":50000000,\"f1\":6000000000,\"points\":3,\"params\":[\"s21\"],\"atten\":0}");
    assert(has("\"field\":\"z\""));
    cmd("{\"cmd\":\"sweep\",\"id\":1,\"f0\":50000000,\"f1\":6000000000,\"points\":3,\"z\":50,\"atten\":0}");
    assert(has("\"field\":\"params\""));
    puts("PASS: every sweep runs at its required attenuation and impedance, which stay the settings afterwards");
}

static void test_status_leds(void) {
    // READY/BUSY show the load: core 1 measuring all the time while core 0 idles (run() never counts core 0's
    // work) is 50 %, both LEDs half on.
    now_us += 1000000; update_status_leds(); // a fresh window
    cmd("{\"cmd\":\"sweep\",\"id\":1,\"f0\":50000000,\"f1\":6000000000,\"points\":201,\"z\":50,"
        "\"params\":[\"s11\",\"s21\",\"s12\",\"s22\"],\"atten\":0}");
    assert(has("\"pt\":200"));
    update_status_leds();
    printf("sweep load: BUSY %u/1000\n", pwm_level[GPIO_LED_BUSY]);
    // Linear brightness: 50 % load is 50 % duty on each LED.
    assert(pwm_level[GPIO_LED_BUSY] >= 495 && pwm_level[GPIO_LED_BUSY] <= 505);
    assert(pwm_level[GPIO_LED_READY] >= 495 && pwm_level[GPIO_LED_READY] <= 505);
    assert(pwm_level[GPIO_LED_BUSY] + pwm_level[GPIO_LED_READY] == 1000);
    assert_rf_off();
    now_us += 1000000; update_status_leds(); // closes the window with the sweep's tail
    now_us += 1000000; update_status_leds(); // nothing to do: READY only
    assert(pwm_level[GPIO_LED_BUSY] == 0 && pwm_level[GPIO_LED_READY] == 1000);
    puts("PASS: READY/BUSY brightness follows the cores' load");
}

static void test_invalid_commands_and_storage(void) {
    const char *bad[] = {
        "{\"cmd\":\"sweep\",\"f0\":49999999,\"f1\":6000000000,\"points\":3,\"z\":50,\"atten\":0,\"params\":[\"s11\"]}",
        "{\"cmd\":\"manual\",\"block\":\"src_freq\",\"value\":49999999}",
        "{\"cmd\":\"cal_set\",\"set\":\"s11_50\",\"f0\":49999999,\"f1\":6000000000,\"points\":[0]}",
        "{\"cmd\":\"cal_set\",\"set\":\"s11_99\",\"f0\":50000000,\"f1\":6000000000,\"points\":[0]}",
        "{\"cmd\":\"cal_get\"}",
        "{\"cmd\":\"sdi_test_set\",\"points\":[{\"f\":49999999,\"thresh_db\":-10,\"s_param\":\"s11\",\"cmp\":\"lt\"}]}",
        "{\"cmd\":\"sdi_test_set\",\"points\":[{\"f\":6000000001,\"thresh_db\":-3,\"cmp\":\"lt\",\"s_param\":\"s11\"}]}",
        "{\"cmd\":\"sdi_test_set\",\"points\":[{\"f\":100000000,\"thresh_db\":-3,\"cmp\":\"typo\",\"s_param\":\"s11\"}]}",
        "{\"cmd\":\"sweep\",\"f0\":50000000,\"f1\":6000000000,\"points\":3,\"z\":50,\"atten\":128,\"params\":[\"s11\"]}",
        "{\"cmd\":\"sweep\",\"f0\":-1,\"f1\":6000000000,\"points\":3,\"z\":50,\"atten\":0,\"params\":[\"s11\"]}",
        "{\"cmd\":\"sweep\",\"f0\":6000000000,\"f1\":50000000,\"points\":3,\"z\":50,\"atten\":0,\"params\":[\"s11\"]}",
        "{\"cmd\":\"sweep\",\"f0\":50000000,\"f1\":6000000000,\"points\":1.5,\"z\":50,\"atten\":0,\"params\":[\"s11\"]}",
        "{\"cmd\":\"sweep\",\"f0\":50000000,\"f1\":6000000000,\"points\":3,\"z\":51,\"atten\":0,\"params\":[\"s11\"]}",
        "{\"cmd\":\"sweep\",\"f0\":50000000,\"f1\":6000000000,\"points\":3,\"z\":50,\"atten\":0,\"params\":[]}",
        "{\"cmd\":\"sweep\",\"f0\":50000000,\"f1\":6000000000,\"points\":3,\"z\":50,\"atten\":0,\"params\":[\"bad\"]}",
        "{\"cmd\":\"sweep\",\"f0\":50000000,\"f1\":6000000000,\"points\":3,\"z\":50,\"atten\":0,\"params\":[\"s11\"],\"log\":1}",
        "{\"cmd\":\"manual\",\"block\":\"src_freq\",\"value\":1e100}",
        "{\"cmd\":\"cal_set\",\"set\":\"s11_50\",\"f0\":50000000,\"f1\":6000000000,\"points\":[1000]}",
        "{\"cmd\":\"set\"}",
        "{\"cmd\":\"set\",\"z\":60}",
        "{\"cmd\":\"set\",\"atten\":128}",
        "{\"cmd\":\"mode_enter\",\"mode\":\"idle\"}",
    };
    for (size_t i = 0; i < count_of(bad); ++i) {
        cmd(bad[i]); assert(has("\"ok\":false,\"error\":\"bad_args\",\"field\":")); assert(reads == 0); assert_rf_off();
    }
    cmd("not json"); assert(has("{\"ok\":false,\"error\":\"bad_request\"}"));
    cmd("{\"id\":4}"); assert(has("\"id\":4,\"ok\":false,\"error\":\"bad_request\""));
    cmd("{\"cmd\":\"adc_read\",\"id\":5}"); assert(has("\"id\":5,\"ok\":false,\"error\":\"unknown_cmd\""));
    cmd("{\"cmd\":\"set_z\",\"id\":5,\"z\":50}"); assert(has("unknown_cmd"));
    write_failure = true;
    cmd("{\"cmd\":\"cal_clear\",\"set\":\"all\"}"); assert(has("storage_error"));
    cmd("{\"cmd\":\"cal_set\",\"set\":\"s11_50\",\"f0\":50000000,\"f1\":6000000000,\"points\":[1]}"); assert(has("storage_error"));
    write_failure = false;
    cmd("{\"cmd\":\"cal_set\",\"set\":\"s11_50\",\"f0\":50000000,\"f1\":6000000000,\"points\":[1]}"); assert(has("\"ok\":true"));
    cmd("{\"cmd\":\"cal_set\",\"set\":\"s21_50\",\"f0\":60000000,\"f1\":6000000000,\"points\":[1]}"); assert(has("grid_mismatch"));
    cmd("{\"cmd\":\"cal_get\",\"set\":\"s21_50\"}"); assert(has("not_calibrated"));
    cmd("{\"cmd\":\"cal_get\",\"id\":3,\"set\":\"s11_50\"}");
    assert(has("\"id\":3,\"ok\":true,\"set\":\"s11_50\",\"f0\":50000000,\"f1\":6000000000,\"points\":[1,1,"));
    cmd("{\"cmd\":\"cal_clear\",\"set\":\"all\"}"); assert(has("\"ok\":true"));
    cmd("{\"cmd\":\"ping\",\"id\":7}");
    assert(has("\"id\":7,\"ok\":true,\"fw\":\"0.4.1\",\"proto\":3,\"hw_rev\":1,\"freq_min_hz\":50000000,\"freq_max_hz\":6000000000"));
    // SDI points up to 6 GHz round-trip.
    cmd("{\"cmd\":\"sdi_test_set\",\"points\":[{\"f\":5940000000,\"thresh_db\":-12.5,\"s_param\":\"s21\",\"cmp\":\"gt\"}]}");
    assert(has("\"ok\":true"));
    cmd("{\"cmd\":\"sdi_test_get\",\"id\":8}");
    assert(has("\"id\":8,\"ok\":true,\"points\":[{\"f\":5940000000,\"thresh_db\":-12.5,\"s_param\":\"s21\",\"cmp\":\"gt\"}]"));
    puts("PASS: invalid commands name their field, EEPROM errors, calibration and SDI storage, ping");
}

// On-board SDI test.
static void test_sdi_front_panel(void) {
    exit_mode(); // the on-board test only starts from idle
    sdi_test_point_t threshold = {.freq_hz = 100000000, .thresh_db_x100 = -500, .s_param = 0};
    assert(eeprom_sdi_test_set(&threshold, 1));
    // It sweeps at 0 dB attenuation, the calibration level, whatever the current setting is
    // (the detector stub checks every source-on read against rf_app_get_atten()), also without a host.
    rf_app_set_atten(127);
    reset_io(""); connected = false; sdi_test_start(); run();
    assert(reads == 2 * RF_CTRL_DETECTOR_SAMPLES && output_len == 0); assert_rf_off();
    assert(rf_app_get_atten() == 0 && rf_app_get_mode() == RF_APP_IDLE);
    sdi_test_update_leds(now_ms() + 2000); // let the result pulse finish
    // A host gets the result; it cannot pass on an unlocked measurement.
    reset_io(""); locked = false; sdi_test_start(); run();
    assert(has("\"event\":\"sdi_test_result\"") && has("\"pass\":false") && reads == 0); assert_rf_off(); locked = true;
    sdi_test_update_leds(now_ms() + 2000);
    // Three passing points (S11 -10 dB < -5 dB): host commands other than housekeeping are busy meanwhile.
    sdi_test_point_t display_points[3] = {threshold, threshold, threshold};
    assert(eeprom_sdi_test_set(display_points, 3));
    reset_io("{\"cmd\":\"ping\",\"id\":81}\n{\"cmd\":\"manual\",\"id\":82,\"block\":\"port\",\"value\":2}\n");
    sdi_test_start(); assert(sdi_test_running()); run();
    assert(has("\"id\":81,\"ok\":true") && has("\"id\":82,\"ok\":false,\"error\":\"busy\""));
    cJSON *evt = cJSON_Parse(strstr(output, "{\"event\""));
    assert(evt && cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(evt, "pass")));
    cJSON *results = cJSON_GetObjectItemCaseSensitive(evt, "results");
    assert(cJSON_GetArraySize(results) == 3);
    cJSON *first = cJSON_GetArrayItem(results, 0);
    assert(strcmp(cJSON_GetObjectItemCaseSensitive(first, "s_param")->valuestring, "s11") == 0);
    assert(fabs(cJSON_GetObjectItemCaseSensitive(first, "val_db")->valuedouble + 10) < 0.05);
    cJSON_Delete(evt);
    assert(!sdi_test_running() && rf_app_get_mode() == RF_APP_IDLE); assert_rf_off();
    // One green pulse per point, then dark.
    uint32_t t0 = now_ms();
    sdi_test_update_leds(t0); assert(pins[GPIO_SDI_TEST_PASS_LED] && !pins[GPIO_SDI_TEST_FAIL_LED]);
    sdi_test_update_leds(t0 + 350); assert(!pins[GPIO_SDI_TEST_PASS_LED]);
    sdi_test_update_leds(t0 + 900); assert(pins[GPIO_SDI_TEST_PASS_LED]);
    sdi_test_update_leds(t0 + 1400); assert(!pins[GPIO_SDI_TEST_PASS_LED] && !pins[GPIO_SDI_TEST_FAIL_LED]);
    // Abort cancels it: no result.
    reset_io("{\"cmd\":\"abort\",\"id\":83}\n"); sdi_test_start(); run();
    assert(!has("sdi_test_result") && rf_app_get_mode() == RF_APP_IDLE); assert_rf_off();
    // Not idle: the press is refused with one red pulse, nothing is measured.
    enter_mode("manual");
    reset_io(""); uint32_t pressed = now_ms(); now_us += 1000; sdi_test_start();
    // Refused on the spot: no request in flight, so the host's commands are never `busy` meanwhile.
    assert(!sdi_test_running() && !protocol_is_sweeping());
    // The main loop's time was read before the press: the red pulse still starts.
    sdi_test_update_leds(pressed); assert(pins[GPIO_SDI_TEST_FAIL_LED]);
    run();
    assert(reads == 0 && rf_app_get_mode() == RF_APP_MANUAL);
    t0 = now_ms();
    sdi_test_update_leds(t0); assert(pins[GPIO_SDI_TEST_FAIL_LED] && !pins[GPIO_SDI_TEST_PASS_LED]);
    sdi_test_update_leds(t0 + 200); assert(!pins[GPIO_SDI_TEST_FAIL_LED]);
    exit_mode();
    puts("PASS: on-board SDI test runs from idle at 0 dB with or without a host, busy meanwhile, pulses per point, abortable");
}

static void test_oversized_line(void) {
    // Oversized input must discard its tail, even if that tail is itself a valid command.
    reset_io(""); memset(input, 'x', 9000);
    strcpy(input + 9000, "{\"cmd\":\"ping\",\"id\":99}\n{\"cmd\":\"ping\",\"id\":100}\n");
    input_len = strlen(input); run();
    assert(!has("\"id\":99")); assert(has("\"id\":100"));
    assert(strstr(output, "{\"ok\":false,\"error\":\"bad_request\"}") < strstr(output, "\"id\":100")); // the long line is answered too
    puts("PASS: oversized-line resynchronization");
}

// CW generator.
static void test_generator(void) {
    rf_app_set_atten(127); rf_app_set_z(50);
    enter_mode("generator");
    cmd("{\"cmd\":\"gen_start\",\"id\":1,\"port\":2,\"z\":75,\"atten\":40,\"f\":2000000000}");
    assert(has("\"ok\":true"));
    assert(gen_active() && !output_terminated() && rf_app_get_port() == 2 && rf_app_get_z() == 75);
    assert(hw_atten == 40 && source_enabled && amp_enabled);
    assert((pins[GPIO_SW_PORT_C1] | pins[GPIO_SW_PORT_C2] << 1) == 1); // port 2
    cmd("{\"cmd\":\"status\",\"id\":2}");
    assert(has("\"gen_on\":true,\"gen_f\":2000000000") && has("\"mode\":\"generator\"") && has("\"init_errors\":[]"));
    // The power meter is its own mode: refused in the generator mode.
    cmd("{\"cmd\":\"power_read\",\"id\":3,\"port\":1,\"z\":50}");
    assert(has("\"error\":\"wrong_mode\",\"mode\":\"generator\"") && gen_active() && reads == 0);
    cmd("{\"cmd\":\"gen_stop\",\"id\":4}");
    assert(has("\"ok\":true")); assert(!gen_active()); assert_rf_off();
    cmd("{\"cmd\":\"status\",\"id\":5}");
    assert(has("\"gen_on\":false") && !has("gen_f"));
    // Lock failure leaves the output terminated and the generator off.
    locked = false;
    cmd("{\"cmd\":\"gen_start\",\"id\":6,\"port\":1,\"z\":50,\"atten\":0,\"f\":100000000}");
    assert(has("source_not_locked")); assert(!gen_active()); assert_rf_off();
    // mode_exit gives up a start still waiting for lock right away.
    uint64_t before = now_us;
    batch("{\"cmd\":\"gen_start\",\"id\":7,\"port\":1,\"z\":50,\"atten\":0,\"f\":100000000}\n{\"cmd\":\"mode_exit\",\"id\":8}\n");
    assert(has("\"id\":7,\"ok\":false,\"error\":\"source_not_locked\"") && has("\"id\":8,\"ok\":true,\"mode\":\"idle\""));
    assert(now_us - before < 100000); assert(!gen_active()); assert_rf_off(); locked = true;
    enter_mode("generator");
    // Bad arguments are rejected before any RF change.
    const char *bad_gen[] = {
        "{\"cmd\":\"gen_start\",\"port\":3,\"z\":50,\"atten\":0,\"f\":100000000}",
        "{\"cmd\":\"gen_start\",\"port\":1,\"z\":60,\"atten\":0,\"f\":100000000}",
        "{\"cmd\":\"gen_start\",\"port\":1,\"z\":50,\"atten\":128,\"f\":100000000}",
        "{\"cmd\":\"gen_start\",\"port\":1,\"z\":50,\"atten\":0,\"f\":49999999}",
        "{\"cmd\":\"gen_start\",\"port\":1,\"z\":50,\"f\":100000000}",
        "{\"cmd\":\"power_read\",\"port\":0,\"z\":50}",
        "{\"cmd\":\"power_read\",\"port\":1,\"z\":51}",
    };
    for (size_t i = 0; i < count_of(bad_gen); ++i) {
        cmd(bad_gen[i]); assert(has("bad_args")); assert(!gen_active()); assert_rf_off();
    }
    // Interlock: a running generator is never taken over by a command.
    cmd("{\"cmd\":\"gen_start\",\"id\":1,\"port\":1,\"z\":50,\"atten\":10,\"f\":1000000000}"); assert(gen_active());
    const struct { const char *line, *error; } refused_under_generator[] = {
        {SWEEP_STRICT, "wrong_mode"},
        {"{\"cmd\":\"manual\",\"id\":1,\"block\":\"port\",\"value\":2}", "wrong_mode"},
        {"{\"cmd\":\"manual\",\"id\":1,\"block\":\"src_freq\",\"value\":2000000000}", "wrong_mode"},
        {"{\"cmd\":\"manual\",\"id\":1,\"block\":\"src_en\",\"value\":false}", "wrong_mode"},
        {"{\"cmd\":\"manual\",\"id\":1,\"block\":\"lpf\",\"value\":2}", "wrong_mode"},
        {"{\"cmd\":\"manual\",\"id\":1,\"block\":\"cplr\",\"value\":1}", "wrong_mode"},
        {"{\"cmd\":\"mode_enter\",\"id\":1,\"mode\":\"manual\"}", "mode_active"},
    };
    for (size_t i = 0; i < count_of(refused_under_generator); ++i) {
        cmd(refused_under_generator[i].line);
        assert(has(refused_under_generator[i].error) && has("\"mode\":\"generator\""));
        assert(gen_active() && !output_terminated() && rf_app_get_z() == 50 && rf_app_get_port() == 1);
        assert(reads == 0);
    }
    // ...except the settings: impedance (hot switched) and level change live.
    cmd("{\"cmd\":\"set\",\"id\":1,\"z\":75}");
    assert(has("\"ok\":true,\"z\":75,\"atten\":10") && gen_active() && rf_app_get_z() == 75);
    cmd("{\"cmd\":\"set\",\"id\":1,\"z\":50,\"atten\":5}");
    assert(has("\"ok\":true,\"z\":50,\"atten\":5") && gen_active() && hw_atten == 5);
    // Host gone: the mode is cancelled, the output parked.
    reset_io(""); connected = false; run();
    assert(!gen_active() && rf_app_get_mode() == RF_APP_IDLE); assert_rf_off();
    cmd("{\"cmd\":\"status\",\"id\":9}"); assert(has("\"mode\":\"idle\""));
    puts("PASS: generator start/stop, lock failure, cancelled start, argument checks, interlocks, live settings, host gone");
}

// Power meter.
static void test_power_meter(void) {
    rf_app_set_atten(127);
    enter_mode("power_meter");
    for (uint8_t port = 1; port <= 2; ++port) {
        parked_read_path = 99;
        char line[128];
        snprintf(line, sizeof(line), "{\"cmd\":\"power_read\",\"id\":7,\"port\":%u,\"z\":75}", port);
        cmd(line);
        cJSON *rec = first_line();
        assert(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(rec, "ok")));
        assert(cJSON_GetObjectItemCaseSensitive(rec, "port")->valueint == port);
        assert(cJSON_GetObjectItemCaseSensitive(rec, "raw")->valueint == 1500);
        double dbm = cJSON_GetObjectItemCaseSensitive(rec, "dbm")->valuedouble;
        assert(fabs(dbm - (20.0 + (1500 / 4095.0 * 3.3) / -0.0245)) < 0.06);
        cJSON_Delete(rec);
        assert(parked_read_path == (port == 1 ? RF_CTRL_COUPLER_PORT1_BWD : RF_CTRL_COUPLER_PORT2_BWD));
        assert(reads == RF_CTRL_DETECTOR_SAMPLES && rf_app_get_z() == 75);
        assert(output_terminated() && hw_atten == 127 && !gen_active());
    }
    exit_mode();
    puts("PASS: power meter reads the backward coupler path of the chosen port with the source parked");
}

// Continuous power meter: power_start / events / power_stop.
static void test_power_stream(void) {
    cmd("{\"cmd\":\"power_start\",\"id\":1,\"port\":1,\"z\":50}");
    assert(has("\"error\":\"wrong_mode\",\"mode\":\"idle\""));
    enter_mode("power_meter");
    const char *bad_power[] = {
        "{\"cmd\":\"power_start\",\"id\":1,\"port\":3,\"z\":50}",
        "{\"cmd\":\"power_start\",\"id\":1,\"port\":1,\"z\":60}",
        "{\"cmd\":\"power_start\",\"id\":1,\"port\":1,\"z\":50,\"interval_ms\":19}",
        "{\"cmd\":\"power_start\",\"id\":1,\"port\":1,\"z\":50,\"interval_ms\":10001}",
    };
    for (size_t i = 0; i < count_of(bad_power); ++i) {
        cmd(bad_power[i]); assert(has("bad_args") && !rf_app_power_running());
    }
    batch(""); assert(output_len == 0); // nothing is streamed before power_start
    parked_read_path = 99;
    cmd("{\"cmd\":\"power_start\",\"id\":2,\"port\":2,\"z\":75,\"interval_ms\":100}");
    assert(has("\"id\":2,\"ok\":true,\"port\":2,\"z\":75,\"interval_ms\":100"));
    assert(rf_app_power_running() && rf_app_get_z() == 75 && output_terminated() && hw_atten == 127);
    // The first event right away, on the path set up once.
    cJSON *power = cJSON_Parse(strstr(output, "{\"event\""));
    assert(power && strcmp(cJSON_GetObjectItemCaseSensitive(power, "event")->valuestring, "power") == 0);
    assert(cJSON_GetObjectItemCaseSensitive(power, "port")->valueint == 2);
    assert(cJSON_GetObjectItemCaseSensitive(power, "z")->valueint == 75);
    assert(cJSON_GetObjectItemCaseSensitive(power, "raw")->valueint == 1500);
    assert(cJSON_GetObjectItemCaseSensitive(power, "dbm") && cJSON_GetObjectItemCaseSensitive(power, "range"));
    cJSON_Delete(power);
    assert(parked_read_path == RF_CTRL_COUPLER_PORT2_BWD && reads == RF_CTRL_DETECTOR_SAMPLES); // the event only
    batch(""); assert(output_len == 0 && reads == 0); // not due yet
    now_us += 100000;
    batch(""); assert(has("\"event\":\"power\""));
    // After a long stall: one event, not a burst to catch up.
    now_us += 1000000;
    batch(""); assert(has("\"event\":\"power\"") && strrchr(output, '{') == output);
    // While it runs: status shows it, single readings would move its path.
    cmd("{\"cmd\":\"status\",\"id\":3}");
    assert(has("\"pwr_on\":true,\"pwr_port\":2") && has("\"mode\":\"power_meter\""));
    cmd("{\"cmd\":\"power_read\",\"id\":4,\"port\":1,\"z\":50}");
    assert(has("\"id\":4,\"ok\":false,\"error\":\"busy\""));
    // A second power_start switches the settings.
    now_us += 1000000;
    cmd("{\"cmd\":\"power_start\",\"id\":5,\"port\":1,\"z\":50}");
    assert(has("\"interval_ms\":250") && rf_app_power_port() == 1 && rf_app_get_z() == 50);
    assert(has("\"event\":\"power\",\"port\":1,\"z\":50") && parked_read_path == RF_CTRL_COUPLER_PORT1_BWD);
    // power_stop ends the stream, the mode stays; single readings work again.
    cmd("{\"cmd\":\"power_stop\",\"id\":6}");
    assert(has("\"ok\":true") && !rf_app_power_running() && rf_app_get_mode() == RF_APP_PWR_DETECTOR);
    now_us += 1000000; batch(""); assert(output_len == 0);
    cmd("{\"cmd\":\"status\",\"id\":7}");
    assert(has("\"pwr_on\":false") && !has("pwr_port"));
    cmd("{\"cmd\":\"power_read\",\"id\":8,\"port\":1,\"z\":50}"); assert(has("\"id\":8,\"ok\":true"));
    // A vanished host ends the stream and the mode.
    cmd("{\"cmd\":\"power_start\",\"id\":9,\"port\":2,\"z\":50}");
    now_us += 1000000; reset_io(""); connected = false; run();
    assert(reads == 0 && output_len == 0 && !rf_app_power_running() && rf_app_get_mode() == RF_APP_IDLE);
    assert_rf_off();
    puts("PASS: continuous power meter streams events at its interval, refuses single reads, stops on power_stop and host loss");
}

// Invariants: no source on without src_en, no live retune, only valid coupler/LPF states.
static void test_manual_invariants(void) {
    rf_app_set_atten(127);
    enter_mode("manual");
    cmd("{\"cmd\":\"manual\",\"id\":1,\"block\":\"src_en\",\"value\":false}");
    assert(rf_state_get_source() == RF_STATE_SOURCE_OFF && !source_enabled && !amp_enabled && hw_atten == 127);
    cmd("{\"cmd\":\"manual\",\"id\":1,\"block\":\"port\",\"value\":2}");
    assert(has("\"source_on\":false") && output_terminated() && rf_app_get_port() == 2); // remembered, source not on
    cmd("{\"cmd\":\"manual\",\"id\":1,\"block\":\"src_en\",\"value\":true}");
    cmd("{\"cmd\":\"manual\",\"id\":1,\"block\":\"port\",\"value\":1}");
    assert(has("\"source_on\":true") && !output_terminated() && rf_app_get_port() == 1);
    cmd("{\"cmd\":\"status\",\"id\":9}"); assert(has("\"mode\":\"manual\""));
    // Source on: retune / filter / coupler writes are refused...
    const char *needs_parked[] = {
        "{\"cmd\":\"manual\",\"id\":1,\"block\":\"src_freq\",\"value\":2000000000}",
        "{\"cmd\":\"manual\",\"id\":1,\"block\":\"lpf\",\"value\":1}",
        "{\"cmd\":\"manual\",\"id\":1,\"block\":\"cplr\",\"value\":2}",
    };
    for (size_t i = 0; i < count_of(needs_parked); ++i) {
        cmd(needs_parked[i]); assert(has("output_active") && !output_terminated());
    }
    // ...the impedance may be switched under a live output, and disabling the source terminates it.
    cmd("{\"cmd\":\"set\",\"id\":1,\"z\":50}");
    assert(rf_app_get_z() == 50 && pins[GPIO_LED_PORT_Z50] && !output_terminated());
    // Settings and the amplifier are no manual blocks.
    const char *not_blocks[] = {
        "{\"cmd\":\"manual\",\"id\":1,\"block\":\"amp_en\",\"value\":false}",
        "{\"cmd\":\"manual\",\"id\":1,\"block\":\"atten\",\"value\":10}",
        "{\"cmd\":\"manual\",\"id\":1,\"block\":\"z\",\"value\":75}",
        "{\"cmd\":\"manual\",\"id\":1}",
    };
    for (size_t i = 0; i < count_of(not_blocks); ++i) {
        cmd(not_blocks[i]); assert(has("\"error\":\"bad_args\",\"field\":\"block\"") && !output_terminated() && amp_enabled);
    }
    cmd("{\"cmd\":\"manual\",\"id\":1,\"block\":\"src_en\",\"value\":false}");
    assert(output_terminated() && !source_enabled && !amp_enabled && hw_atten == 127);
    cmd("{\"cmd\":\"manual\",\"id\":1,\"block\":\"port\",\"value\":1}");
    assert(has("\"source_on\":false") && output_terminated()); // source off
    cmd("{\"cmd\":\"manual\",\"id\":1,\"block\":\"src_en\",\"value\":true}");
    // Only the four defined coupler configurations and four LPF states exist.
    const char *bad_manual[] = {
        "{\"cmd\":\"manual\",\"id\":1,\"block\":\"cplr\",\"value\":4}",
        "{\"cmd\":\"manual\",\"id\":1,\"block\":\"cplr\",\"value\":4095}",
        "{\"cmd\":\"manual\",\"id\":1,\"block\":\"lpf\",\"value\":4}",
        "{\"cmd\":\"manual\",\"id\":1,\"block\":\"port\",\"value\":3}",
        "{\"cmd\":\"manual\",\"id\":1,\"block\":\"port\",\"value\":-1}",
        "{\"cmd\":\"manual\",\"id\":1,\"block\":\"src_en\",\"value\":1}",
    };
    for (size_t i = 0; i < count_of(bad_manual); ++i) {
        cmd(bad_manual[i]); assert(has("\"error\":\"bad_args\",\"field\":\"value\""));
    }
    cmd("{\"cmd\":\"manual\",\"id\":1,\"block\":\"cplr\",\"value\":3}");
    assert(has("\"ok\":true") && selected_path() == RF_CTRL_COUPLER_PORT2_BWD);
    rf_app_set_z(60); assert(rf_app_get_z() == 50); // invalid values are ignored, not mapped to 50/75
    rf_state_set_port(7); assert(rf_app_get_port() == 1 || rf_app_get_port() == 2);
    puts("PASS: invariants -- no output without the source, no live retune, only valid coupler/LPF/port values");
}

// Modes: entered and left by the host, each one blocks all others.
static void test_modes(void) {
    exit_mode();
    uint16_t raw16;
    double temp;
    rf_app_detector_sample_t sample;
    // Outside their mode, operations are refused and change nothing.
    assert(rf_app_generator_start(1, 50, 0, 1000000000ULL) == RF_APP_ERR_WRONG_MODE && output_terminated());
    assert(rf_app_generator_stop() == RF_APP_ERR_WRONG_MODE);
    assert(rf_app_power_read(1, 50, &raw16) == RF_APP_ERR_WRONG_MODE);
    assert(rf_app_power_sample(&raw16) == RF_APP_ERR_WRONG_MODE);
    assert(rf_app_manual_lpf(1) == RF_APP_ERR_WRONG_MODE && rf_app_manual_port(1) == RF_APP_ERR_WRONG_MODE);
    assert(rf_app_probe_path(RF_APP_PATH_PORT1_FWD, &sample) == RF_APP_ERR_WRONG_MODE);
    assert(!rf_app_sweep_begin(50, 1000000000ULL, false, 0));
    cmd(SWEEP_STRICT);
    assert(has("\"error\":\"wrong_mode\",\"mode\":\"idle\"") && reads == 0 && !protocol_is_sweeping());
    // Settings work in every mode and never change it.
    assert(rf_app_set_atten(20) == RF_APP_OK && rf_app_set_z(75) == RF_APP_OK && rf_app_get_mode() == RF_APP_IDLE);
    assert(rf_app_set_z(50) == RF_APP_OK && rf_app_set_atten(127) == RF_APP_OK);
    // A mode is entered only from idle: every mode blocks every other one (and itself).
    const char *names[] = { "sweep", "generator", "power_meter", "manual" };
    for (size_t i = 0; i < count_of(names); ++i) {
        enter_mode(names[i]);
        char expected[64];
        snprintf(expected, sizeof(expected), "\"error\":\"mode_active\",\"mode\":\"%s\"", names[i]);
        for (size_t j = 0; j < count_of(names); ++j) {
            char line[96];
            snprintf(line, sizeof(line), "{\"cmd\":\"mode_enter\",\"id\":3,\"mode\":\"%s\"}", names[j]);
            cmd(line); assert(has(expected));
        }
        cmd("{\"cmd\":\"status\",\"id\":4}");
        snprintf(expected, sizeof(expected), "\"mode\":\"%s\"", names[i]);
        assert(has(expected));
        exit_mode();
    }
    cmd("{\"cmd\":\"mode_enter\",\"id\":5,\"mode\":\"demo\"}"); assert(has("bad_args"));
    exit_mode(); // already idle: fine
    // A sweep session cannot be left; the mode outlives its sessions.
    assert(rf_app_mode_enter(RF_APP_SWEEP) == RF_APP_OK);
    assert(rf_app_sweep_begin(50, 1000000000ULL, false, 0));
    assert(!rf_app_sweep_begin(50, 1000000000ULL, false, 0)); // no second session
    assert(rf_app_mode_exit() == RF_APP_ERR_BUSY && rf_app_get_mode() == RF_APP_SWEEP);
    assert(rf_app_read_temp_c(&temp) == RF_APP_ERR_BUSY);
    assert(rf_app_set_z(75) == RF_APP_OK && rf_app_set_atten(0) == RF_APP_OK); // settings: any time
    assert(rf_app_set_z(50) == RF_APP_OK);
    rf_app_sweep_end(); assert(rf_app_get_mode() == RF_APP_SWEEP);
    rf_app_sweep_end(); // no-op without a session
    assert(rf_app_mode_exit() == RF_APP_OK && rf_app_get_mode() == RF_APP_IDLE);
    // Generator: stop keeps the mode, exit turns a running output off.
    assert(rf_app_mode_enter(RF_APP_GENERATOR) == RF_APP_OK);
    assert(rf_app_generator_start(1, 50, 10, 1000000000ULL) == RF_APP_OK && gen_active() && !output_terminated());
    assert(rf_app_set_z(75) == RF_APP_OK && gen_active() && !output_terminated()); // hot switching
    assert(rf_app_set_z(50) == RF_APP_OK);
    assert(rf_app_set_atten(5) == RF_APP_OK && hw_atten == 5); // live level
    assert(rf_app_probe_path(RF_APP_PATH_PORT1_BWD, &sample) == RF_APP_OK && gen_active());
    assert(rf_app_generator_stop() == RF_APP_OK && !gen_active() && rf_app_get_mode() == RF_APP_GENERATOR);
    assert_rf_off();
    assert(rf_app_generator_start(2, 50, 10, 1100000000ULL) == RF_APP_OK && rf_app_get_port() == 2);
    assert(rf_app_mode_exit() == RF_APP_OK && !gen_active()); assert_rf_off();
    // Manual: leaving it restores the idle hardware state, also a switched-off source.
    assert(rf_app_mode_enter(RF_APP_MANUAL) == RF_APP_OK);
    assert(rf_app_manual_lpf(2) == RF_APP_OK && rf_state_get_lpf() == 2);
    assert(rf_app_manual_port(1) == RF_APP_OK && rf_app_source_on());
    assert(rf_app_read_temp_c(&temp) == RF_APP_ERR_OUTPUT_ACTIVE); // never while the source is on
    assert(rf_app_manual_port(0) == RF_APP_OK && !rf_app_source_on() && rf_app_get_mode() == RF_APP_MANUAL);
    assert(rf_app_read_temp_c(&temp) == RF_APP_OK && temp == 25.0);
    assert(rf_app_manual_source(false) == RF_APP_OK && !source_enabled);
    assert(rf_app_mode_exit() == RF_APP_OK && source_enabled); assert_rf_off();
    // Invalid values are rejected before anything else.
    assert(rf_app_manual_coupler(4) == RF_APP_ERR_BAD_ARGS && rf_app_manual_lpf(4) == RF_APP_ERR_BAD_ARGS);
    assert(rf_app_set_atten(128) == RF_APP_ERR_BAD_ARGS && rf_app_set_z(60) == RF_APP_ERR_BAD_ARGS);
    assert(rf_app_mode_enter(RF_APP_IDLE) == RF_APP_ERR_BAD_ARGS);
    puts("PASS: modes -- entered only from idle, each blocks the others, operations only in their mode, exit restores idle");
}

// src_temp_read: live when possible, otherwise the cached value.
static void test_temperature(void) {
    cmd("{\"cmd\":\"src_temp_read\",\"id\":20}");
    assert(has("\"id\":20,\"ok\":true,\"cached\":false,\"temp_c\":25"));
    enter_mode("sweep");
    batch(SWEEP_STRICT "\n{\"cmd\":\"src_temp_read\",\"id\":22}\n");
    assert(has("\"id\":22,\"ok\":true,\"cached\":true,\"age_ms\":"));
    assert_rf_off();
    exit_mode(); enter_mode("manual");
    cmd("{\"cmd\":\"manual\",\"id\":1,\"block\":\"port\",\"value\":1}");
    cmd("{\"cmd\":\"src_temp_read\",\"id\":23}"); assert(has("\"id\":23,\"ok\":true,\"cached\":true")); // source on
    exit_mode();
    puts("PASS: src_temp_read live when possible, cached during a sweep or with the source on");
}

// Detector probe: held route, per-path readings, derived S-parameters.
static void test_probe(void) {
    rf_app_set_atten(127);
    enter_mode("generator");
    cmd("{\"cmd\":\"gen_start\",\"id\":1,\"port\":1,\"z\":50,\"atten\":10,\"f\":1000000000}");
    assert(gen_active());
    cmd("{\"cmd\":\"probe_read\",\"id\":30}");
    cJSON *rec = cJSON_Parse(output); assert(rec);
    assert(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(rec, "gen_on")));
    assert(cJSON_GetObjectItemCaseSensitive(rec, "f")->valuedouble == 1000000000.0);
    cJSON *rd = cJSON_GetObjectItemCaseSensitive(rec, "readings"); assert(cJSON_GetArraySize(rd) == 4);
    for (int i = 0; i < 4; i++) {
        cJSON *r = cJSON_GetArrayItem(rd, i);
        assert(cJSON_GetObjectItemCaseSensitive(r, "path")->valueint == i);
        assert(cJSON_GetObjectItemCaseSensitive(r, "min")->valueint == 0);     // the stub injects ADC spikes:
        assert(cJSON_GetObjectItemCaseSensitive(r, "max")->valueint == 4095); // spread must be visible, median clean
    }
    assert(cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(rd, 1), "raw")->valueint == (int)lround(1.245 * 4095 / 3.3));
    cJSON *d = cJSON_GetObjectItemCaseSensitive(rec, "derived"); assert(d);
    assert(strcmp(cJSON_GetObjectItemCaseSensitive(d, "refl")->valuestring, "s11") == 0);
    assert(fabs(cJSON_GetObjectItemCaseSensitive(d, "refl_db")->valuedouble + 10) < 0.05);
    assert(fabs(cJSON_GetObjectItemCaseSensitive(d, "trans_db")->valuedouble + 20) < 0.05);
    assert(!cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(d, "cal"))); // nothing stored: raw == calibrated
    assert(fabs(cJSON_GetObjectItemCaseSensitive(d, "refl_cal_db")->valuedouble + 10) < 0.05);
    cJSON_Delete(rec);
    assert(gen_active() && !output_terminated() && hw_atten == 10); // the probe never disturbs the route

    // With stored calibration the corrected value is reported next to the raw one.
    for (int idx = 0; idx < 4; ++idx) assert(eeprom_cal_set(idx, 50000000ULL, 6000000000ULL, correction, 1) == EEPROM_CAL_OK);
    cmd("{\"cmd\":\"probe_read\",\"id\":31,\"paths\":[0,1,3]}");
    rec = cJSON_Parse(output); assert(rec);
    d = cJSON_GetObjectItemCaseSensitive(rec, "derived"); assert(d);
    assert(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(d, "cal")));
    assert(fabs(cJSON_GetObjectItemCaseSensitive(d, "refl_db")->valuedouble + 10) < 0.05);
    assert(fabs(cJSON_GetObjectItemCaseSensitive(d, "refl_cal_db")->valuedouble + 15) < 0.05);
    assert(fabs(cJSON_GetObjectItemCaseSensitive(d, "trans_cal_db")->valuedouble + 25) < 0.05);
    cJSON_Delete(rec);
    assert(eeprom_cal_clear_all());

    // Source port 2 swaps the roles: S22 uses its own forward/backward paths, S12 the other port's backward path.
    cmd("{\"cmd\":\"gen_start\",\"id\":1,\"port\":2,\"z\":50,\"atten\":10,\"f\":1000000000}");
    cmd("{\"cmd\":\"probe_read\",\"id\":32}");
    rec = cJSON_Parse(output); assert(rec);
    d = cJSON_GetObjectItemCaseSensitive(rec, "derived"); assert(d);
    assert(strcmp(cJSON_GetObjectItemCaseSensitive(d, "refl")->valuestring, "s22") == 0);
    assert(strcmp(cJSON_GetObjectItemCaseSensitive(d, "trans")->valuestring, "s12") == 0);
    assert(fabs(cJSON_GetObjectItemCaseSensitive(d, "refl_db")->valuedouble + 40) < 0.05);
    assert(fabs(cJSON_GetObjectItemCaseSensitive(d, "trans_db")->valuedouble + 30) < 0.05);
    cJSON_Delete(rec);

    // Missing a needed path: readings only, no derived value. Bad path lists are rejected.
    cmd("{\"cmd\":\"probe_read\",\"id\":33,\"paths\":[1]}");
    assert(has("\"readings\"") && !has("\"derived\""));
    const char *bad_probe[] = {
        "{\"cmd\":\"probe_read\",\"paths\":[]}", "{\"cmd\":\"probe_read\",\"paths\":[4]}",
        "{\"cmd\":\"probe_read\",\"paths\":[1,1]}", "{\"cmd\":\"probe_read\",\"paths\":\"all\"}",
    };
    for (size_t i = 0; i < count_of(bad_probe); ++i) { cmd(bad_probe[i]); assert(has("\"field\":\"paths\"")); }
    // Generator off: the output is terminated, readings still work (leakage/noise floor), nothing derived.
    cmd("{\"cmd\":\"gen_stop\",\"id\":1}");
    cmd("{\"cmd\":\"probe_read\",\"id\":34}");
    assert(has("\"gen_on\":false") && has("\"readings\"") && !has("\"derived\"") && !has("\"f\""));
    assert_rf_off();
    // Refused (busy) during a sweep like every other RF command.
    exit_mode(); enter_mode("sweep");
    batch(SWEEP_STRICT "\n{\"cmd\":\"probe_read\",\"id\":35}\n");
    assert(has("\"id\":35,\"ok\":false,\"error\":\"busy\""));
    assert_rf_off();
    puts("PASS: probe reads every coupler path with the route held, shows spread, derives raw and calibrated S-parameters per source port");
}

// Raw detector sweep: the four coupler states instead of S-parameters.
static void test_raw_sweep(void) {
    rf_app_set_atten(127);
    cmd("{\"cmd\":\"sweep\",\"id\":40,\"f0\":100000000,\"f1\":200000000,\"points\":2,\"z\":50,\"port\":1,\"params\":[\"p2_bwd\",\"p1_fwd\",\"p1_bwd\"],\"allow_unlocked\":false,\"atten\":0}");
    assert(has("\"done\":true") && has("\"ok\":true"));
    cJSON *pt = first_line();
    // Source on port 1: the stub's detector volts are {1.0, 1.245, 0.8, 1.49} V for paths 0..3.
    assert(cJSON_GetObjectItemCaseSensitive(pt, "p1_fwd_raw")->valueint == (int)lround(1.0 * 4095 / 3.3));
    assert(cJSON_GetObjectItemCaseSensitive(pt, "p1_bwd_raw")->valueint == (int)lround(1.245 * 4095 / 3.3));
    assert(cJSON_GetObjectItemCaseSensitive(pt, "p2_bwd_raw")->valueint == (int)lround(1.49 * 4095 / 3.3));
    assert(!cJSON_GetObjectItemCaseSensitive(pt, "p2_fwd_raw"));              // not requested
    assert(!cJSON_GetObjectItemCaseSensitive(pt, "s11_db") && !cJSON_GetObjectItemCaseSensitive(pt, "s21_db"));
    assert(fabs(cJSON_GetObjectItemCaseSensitive(pt, "p1_fwd_dbm")->valuedouble - (20.0 + 1.0 / -0.0245)) < 0.06);
    assert(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(pt, "locked")));
    assert(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(pt, "cal")));
    cJSON_Delete(pt);
    assert(reads == 2 * 3 * RF_CTRL_DETECTOR_SAMPLES);
    assert_rf_off();

    // Port 2 routes the other port: different detector values for the same paths.
    cmd("{\"cmd\":\"sweep\",\"id\":41,\"f0\":100000000,\"f1\":100000000,\"points\":1,\"z\":50,\"params\":[\"p2_fwd\",\"p2_bwd\"],\"port\":2,\"atten\":0}");
    pt = first_line();
    assert(cJSON_GetObjectItemCaseSensitive(pt, "p2_fwd_raw")->valueint == (int)lround(1.2 * 4095 / 3.3));
    assert(cJSON_GetObjectItemCaseSensitive(pt, "p2_bwd_raw")->valueint == (int)lround(2.18 * 4095 / 3.3));
    cJSON_Delete(pt);
    assert_rf_off();

    // S-parameters and raw paths cannot be mixed; `port` only belongs to (and is required by) a raw sweep.
    const struct { const char *line, *field; } bad_raw[] = {
        {"{\"cmd\":\"sweep\",\"f0\":100000000,\"f1\":200000000,\"points\":2,\"z\":50,\"atten\":0,\"port\":1,\"params\":[\"s11\",\"p1_fwd\"]}", "params"},
        {"{\"cmd\":\"sweep\",\"f0\":100000000,\"f1\":200000000,\"points\":2,\"z\":50,\"atten\":0,\"params\":[\"s11\"],\"port\":2}", "port"},
        {"{\"cmd\":\"sweep\",\"f0\":100000000,\"f1\":200000000,\"points\":2,\"z\":50,\"atten\":0,\"params\":[\"p1_fwd\"],\"port\":3}", "port"},
        {"{\"cmd\":\"sweep\",\"f0\":100000000,\"f1\":200000000,\"points\":2,\"z\":50,\"atten\":0,\"params\":[\"p1_fwd\"]}", "port"},
        {"{\"cmd\":\"sweep\",\"f0\":100000000,\"f1\":200000000,\"points\":2,\"z\":50,\"atten\":0,\"port\":1,\"params\":[\"p9_fwd\"]}", "params"},
    };
    for (size_t i = 0; i < count_of(bad_raw); ++i) {
        cmd(bad_raw[i].line);
        char expected[48];
        snprintf(expected, sizeof(expected), "\"field\":\"%s\"", bad_raw[i].field);
        assert(has(expected) && reads == 0); assert_rf_off();
    }
    puts("PASS: raw detector sweep returns the requested coupler states per source port, rejects mixes with S-parameters");
}

// Bootsel: the mode is left before the reboot.
static void test_bootsel(void) {
    exit_mode(); enter_mode("manual");
    cmd("{\"cmd\":\"bootsel\",\"id\":50}");
    assert(has("\"id\":50,\"ok\":true") && rebooted && rf_app_get_mode() == RF_APP_IDLE);
    puts("PASS: bootsel leaves the mode, answers, then reboots");
}

// Inputs that once could take the device down for good.
static void test_robustness(void) {
    // Deep nesting is refused by the parser (CJSON_NESTING_LIMIT) instead of recursing through the stack.
    char deep[2048] = {0};
    size_t n = 0;
    for (int i = 0; i < 500; i++) deep[n++] = '[';
    for (int i = 0; i < 500; i++) deep[n++] = ']';
    deep[n++] = '\n';
    batch(deep);
    assert(has("\"ok\":false,\"error\":\"bad_request\""));
    batch("{\"cmd\":\"ping\",\"id\":[[[[[[[[[[[[1]]]]]]]]]]]]}\n");
    assert(has("bad_request") && !has("\"fw\""));
    batch("{\"cmd\":\"ping\",\"id\":[[1]]}\n");
    assert(has("\"id\":[[1]],\"ok\":true"));

    // Commands the old session left unread never run in the next one.
    reset_io("{\"cmd\":\"mode_enter\",\"id\":5,\"mode\":\"generator\"}\n");
    connected = false;
    unsigned flushes = rx_flushes, clears = tx_clears;
    run();
    assert(rx_flushes > flushes && tx_clears == clears + 1 && input_pos == input_len);
    connected = true; run();
    assert(rf_app_get_mode() == RF_APP_IDLE && output_len == 0);

    // A host that stops reading outside a sweep costs one stall, not one per line.
    reset_io("");
    stalled = true;
    uint64_t before = now_us;
    for (int i = 0; i < 5; i++) protocol_handle_line("{\"cmd\":\"ping\",\"id\":1}");
    assert(now_us - before < 3000000u); // one 2 s stall, the other lines dropped at once
    stalled = false;
    cmd("{\"cmd\":\"ping\",\"id\":2}");
    assert(has("\"id\":2,\"ok\":true"));
    puts("PASS: deep nesting, stale input after a disconnect, stalled host outside a sweep");
}

int main(void) {
    test_boot();
    test_app_sweeps();
    test_protocol_sweeps();
    test_status_leds();
    test_invalid_commands_and_storage();
    test_sdi_front_panel();
    test_oversized_line();
    test_generator();
    test_power_meter();
    test_power_stream();
    test_manual_invariants();
    test_modes();
    test_temperature();
    test_probe();
    test_raw_sweep();
    test_bootsel();
    test_robustness();
    return 0;
}
