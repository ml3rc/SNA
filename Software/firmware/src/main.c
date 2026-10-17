#include <math.h>

#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include "hardware/watchdog.h"
#include "tusb.h"

#include "sna.h"

#include "eeprom.h"
#include "main.h"
#include "protocol.h"
#include "rf_task.h"
#include "sdi_test.h"

// The longest request, cal_set with EEPROM_CAL_POINTS_MAX values, is about
// 5 KB; a longer line is answered with bad_request and discarded.
#define LINE_BUF_SIZE 8192

#define HW_STARTUP_TIME 200
#define USB_RESET_TIME 250

// The watchdog resets the device when core 0 stops running its loops (a hung
// I2C transfer, say) or core 1 sits in one step for CORE1_STUCK_MS. The longest
// legitimate step is a 2 s lock wait, plus up to WRITE_STALL_MS (2 s) waiting for
// room in the reply queue while core 0 writes to a host that stopped reading.
#define WATCHDOG_TIMEOUT_MS 8000
#define CORE1_STUCK_MS 6000

static char line_buf[LINE_BUF_SIZE];
static size_t line_len = 0;
static bool line_overflow = false;

// Init checks that failed (init_error_t bits), 0 once boot went fine.
static uint8_t init_errors;

uint8_t get_init_errors(void) { return init_errors; }

_Static_assert(INIT_ERR_USB == 1u << (INIT_ERROR_CODES - 1), "INIT_ERROR_CODES must count every init_error_t bit");

// Board-level GPIOs that aren't part of any chip driver in src/hal/ --
// everything RF-chip-specific (attenuator, switches, MAX2871, detector,
// amp, EEPROM/I2C, ADC) is initialized by its own HAL module instead, via
// rf_task_start() (on core 1) / eeprom_init() below. This is just the front-panel
// LEDs and the SDI self-test button.
static void board_gpio_init(void) {
    const uint out_pins[] = {
        // SDI self-test LEDs
        GPIO_SDI_TEST_PASS_LED, GPIO_SDI_TEST_FAIL_LED,
        // Status LEDs
        GPIO_LED_PORT1_OUT_EN, GPIO_LED_PORT_Z50, GPIO_LED_READY,
        GPIO_LED_BUSY, GPIO_LED_BOOTED,
    };
    for (size_t i = 0; i < count_of(out_pins); i++) {
        gpio_init(out_pins[i]);
        gpio_set_dir(out_pins[i], GPIO_OUT);
    }

    gpio_set_drive_strength(GPIO_LED_PORT1_OUT_EN, GPIO_DRIVE_STRENGTH_12MA);
    gpio_set_drive_strength(GPIO_LED_PORT_Z50, GPIO_DRIVE_STRENGTH_12MA);

    // SDI self-test button (active-low, pulled up)
    gpio_init(GPIO_SDI_TEST_BUTTON);
    gpio_set_dir(GPIO_SDI_TEST_BUTTON, GPIO_IN);
    gpio_pull_up(GPIO_SDI_TEST_BUTTON);
}

// Fast BOOTED blink while init runs. A timer interrupt, because core 0 blocks
// in sleeps and waits for core 1's RF init meanwhile.
#define BOOT_BLINK_HALF_PERIOD_MS 50

static bool boot_blink_toggle(repeating_timer_t *timer) {
    (void) timer;
    gpio_put(GPIO_LED_BOOTED, !gpio_get(GPIO_LED_BOOTED));
    return true; // keep repeating
}

// Non-blocking debounce for the SDI self-test button (active-low, pulled
// up): unlike a sleep_ms()-based debounce, this never stalls the main loop
// (USB/protocol handling, heartbeats, etc. keep running through a bounce).
// button_press_edge() returns true exactly once, on the iteration where a
// high->low transition has been stable for BUTTON_DEBOUNCE_MS.
#define BUTTON_DEBOUNCE_MS 30

typedef struct {
    bool raw_prev;
    bool stable;
    uint32_t last_change_ms;
} debounced_button_t;

static bool button_press_edge(debounced_button_t *b, bool raw_now, uint32_t now_ms) {
    if (raw_now != b->raw_prev) {
        b->last_change_ms = now_ms;
        b->raw_prev = raw_now;
    }
    if (raw_now != b->stable && (now_ms - b->last_change_ms) >= BUTTON_DEBOUNCE_MS) {
        bool was_high = b->stable; // raw_now != b->stable here, so raw_now is !was_high
        b->stable = raw_now;
        return was_high; // true only for a high->low (press) transition
    }
    return false;
}

// GPIO writes only on a change, so nothing toggles while core 1 samples the ADC.
static void put_if_changed(uint pin, bool value) {
    if (gpio_get(pin) != value) gpio_put(pin, value);
}

// READY/BUSY show the load of both cores: the share of each LOAD_WINDOW_US
// they spent working, averaged over the two (core 1: requests and sweep
// points, rf_task_busy_us(); core 0: main-loop passes that had USB events, a
// command or a reply to handle). BUSY's brightness is the load, READY's the
// rest: idle = READY only, both cores flat out = BUSY only. 
// Hardware PWM, so the duty cycle changes once
// per window and no code toggles the pins.
// PORT1_OUT_EN/PORT_Z50 are not handled here: they follow the port and
// impedance switches directly (rf_control/ports.c).
#define LOAD_WINDOW_US 1000u
#define LED_PWM_TOP 999u        // 1000 brightness steps
#define LED_PWM_CLKDIV 150.0f   // 150 MHz / 150 / 1000 = 1 kHz, no visible flicker
#define LED_PWM_FULL (LED_PWM_TOP + 1u)

static uint32_t core0_busy_us;

static void status_led_pwm_init(uint pin) {
    uint slice = pwm_gpio_to_slice_num(pin);
    pwm_set_wrap(slice, LED_PWM_TOP);
    pwm_set_clkdiv(slice, LED_PWM_CLKDIV);
    pwm_set_enabled(slice, true);
    gpio_set_function(pin, GPIO_FUNC_PWM);
}

void update_status_leds(void) {
    static bool started;
    static uint64_t window_start;
    static uint32_t core0_start, core1_start;
    uint64_t now = time_us_64();
    uint32_t core0 = core0_busy_us, core1 = rf_task_busy_us();
    if (!started) {
        pwm_set_gpio_level(GPIO_LED_BUSY, 0);
        pwm_set_gpio_level(GPIO_LED_READY, LED_PWM_FULL);
        status_led_pwm_init(GPIO_LED_BUSY);
        status_led_pwm_init(GPIO_LED_READY);
        started = true;
    } else {
        uint64_t elapsed = now - window_start;
        if (elapsed < LOAD_WINDOW_US) return;
        uint64_t busy = (uint64_t) (core0 - core0_start) + (core1 - core1_start);
        uint64_t load = busy * LED_PWM_FULL / (2 * elapsed);
        if (load > LED_PWM_FULL) load = LED_PWM_FULL;
        pwm_set_gpio_level(GPIO_LED_BUSY, load);
        pwm_set_gpio_level(GPIO_LED_READY,LED_PWM_FULL - load);
    }
    window_start = now;
    core0_start = core0;
    core1_start = core1;
}

void set_sdi_leds(bool pass, bool fail) {
    put_if_changed(GPIO_SDI_TEST_PASS_LED, pass);
    put_if_changed(GPIO_SDI_TEST_FAIL_LED, fail);
}

// Init error blink code on the red SDI LED: for every failed check (lowest
// first) its number of short blinks (init_error_t), then a pause; repeats
// until reset. Computed from the time alone, so it needs no state.
#define ERROR_BLINK_PERIOD_MS 500
#define ERROR_BLINK_ON_MS 200
#define ERROR_CODE_PAUSE_MS 1500

static void update_error_code(uint32_t now_ms) {
    uint32_t cycle_ms = 0;
    for (unsigned code = 1; code <= INIT_ERROR_CODES; code++)
        if (init_errors & (1u << (code - 1))) cycle_ms += code * ERROR_BLINK_PERIOD_MS + ERROR_CODE_PAUSE_MS;
    if (cycle_ms == 0) return;
    uint32_t t = now_ms % cycle_ms;
    bool on = false;
    for (unsigned code = 1; code <= INIT_ERROR_CODES; code++) {
        if (!(init_errors & (1u << (code - 1)))) continue;
        uint32_t blinks_ms = code * ERROR_BLINK_PERIOD_MS;
        if (t < blinks_ms) { on = t % ERROR_BLINK_PERIOD_MS < ERROR_BLINK_ON_MS; break; }
        t -= blinks_ms;
        if (t < ERROR_CODE_PAUSE_MS) break;
        t -= ERROR_CODE_PAUSE_MS;
    }
    put_if_changed(GPIO_SDI_TEST_FAIL_LED, on);
}

// One 100 ms pulse per second on BOOTED: shows the main loop (or a blocking
// measurement that keeps servicing USB) is alive. A fixed cadence, so a hung
// firmware is visibly stuck on or off. Core 0 services this from both idle
// and blocking protocol/acquisition paths. After a failed init BOOTED stays
// off and the red SDI LED shows the error code instead.
// Fed from update_heartbeat(), which core 0's main loop and its blocking paths
// (long USB writes) both call: only while core 1 is making progress.
static void service_watchdog(uint32_t now_ms) {
    static uint32_t loops, changed_ms;
    uint32_t current = rf_task_core1_loops();
    if (current != loops) { loops = current; changed_ms = now_ms; }
    if (now_ms - changed_ms < CORE1_STUCK_MS) watchdog_update();
}

void update_heartbeat(void) {
    static bool initialized = false;
    static uint32_t start_ms;
    static bool led_state;
    uint32_t now_ms = to_ms_since_boot(get_absolute_time());
    service_watchdog(now_ms);
    if (init_errors) {
        update_error_code(now_ms);
        return;
    }
    if (!initialized) { start_ms = now_ms; initialized = true; }
    uint32_t elapsed = now_ms - start_ms;
    start_ms += (elapsed / 1000) * 1000;
    bool on = (elapsed % 1000) < 100;
    if (on != led_state) { led_state = on; gpio_put(GPIO_LED_BOOTED, on); }
}

bool poll_usb_commands(void) {
    static bool was_connected;
    update_heartbeat();
    if (!tud_cdc_connected()) {
        if (was_connected) {
            // Unsent output of the old session must not lead the next one's replies.
            tud_cdc_write_clear();
            protocol_host_disconnected();
        }
        was_connected = false;
        // Nor may its unread commands run in the next session.
        tud_cdc_read_flush();
        line_len = 0;
        line_overflow = false;
        return false;
    }
    was_connected = true;
    bool received = false;
    // Bound work per poll so a stream of host commands cannot starve RF work.
    uint32_t budget = LINE_BUF_SIZE;
    while (budget-- > 0 && tud_cdc_available()) {
        char c;
        if (tud_cdc_read(&c, 1) == 0) {
            break;
        } 
        received = true;
        if (c == '\n') {
            line_buf[line_len] = '\0';
            bool dispatch = !line_overflow && line_len > 0;
            bool too_long = line_overflow;
            line_len = 0;
            line_overflow = false;
            if (dispatch) protocol_handle_line(line_buf);
            else if (too_long) protocol_reject_line(); // every request gets a reply
        } else if (c != '\r' && !line_overflow) {
            if (line_len < LINE_BUF_SIZE - 1) line_buf[line_len++] = c;
            else line_overflow = true; // discard the ENTIRE line, including its tail
        }
    }
    return received;
}

int main(void) {
    board_gpio_init();
    repeating_timer_t boot_blink;
    add_repeating_timer_ms(BOOT_BLINK_HALF_PERIOD_MS, boot_blink_toggle, NULL, &boot_blink);

    //sleep until hw (reference clocks/ic are stable)
    sleep_ms(HW_STARTUP_TIME);

    // Core 1 initializes the RF hardware while core 0 does the rest.
    rf_task_start();
    uint8_t errors = 0;
    if (!eeprom_init()) errors |= INIT_ERR_EEPROM;
    if (!tusb_init()) errors |= INIT_ERR_USB;
    // A reset can be too brief for the host to notice the old USB device
    // disappeared. Force a visible detach before enumerating a fresh session.
    tud_disconnect();
    sleep_ms(USB_RESET_TIME);
    tud_connect();
    uint8_t rf_errors = rf_task_wait_ready();
    // Missing hooks (a firmware bug) leave the source unusable as well.
    if (rf_errors & (RF_APP_INIT_ERR_NOT_LOCKED | RF_APP_INIT_ERR_NO_HOOKS)) errors |= INIT_ERR_SOURCE;
    if (rf_errors & RF_APP_INIT_ERR_DETECTOR) errors |= INIT_ERR_DETECTOR;

    cancel_repeating_timer(&boot_blink);
    gpio_put(GPIO_LED_BOOTED, false); // the heartbeat takes over, or it stays off on an error
    init_errors = errors;
    // Only now: initialization itself waits up to 2 s for the first PLL lock.
    watchdog_enable(WATCHDOG_TIMEOUT_MS, true);

    debounced_button_t button = { .raw_prev = true, .stable = true, .last_change_ms = 0 };

    while (1) {
        uint64_t pass_start = time_us_64();
        bool worked = tud_task_event_ready();
        tud_task();
        worked |= poll_usb_commands();
        worked |= protocol_poll(); // core 1's replies, power meter events, host gone

        // The red LED shows the init error code; the test would not mean much anyway.
        uint32_t now_ms = to_ms_since_boot(get_absolute_time());
        if (button_press_edge(&button, gpio_get(GPIO_SDI_TEST_BUTTON), now_ms) && !init_errors) {
            sdi_test_start(); // an orange blink without stored SDI points
        }
        if (!init_errors) sdi_test_update_leds(now_ms);

        // A pass with nothing to do is idle polling; one that worked counts towards core 0's load.
        if (worked) core0_busy_us += (uint32_t) (time_us_64() - pass_start);
        update_status_leds();
    }

    return 0;
}
