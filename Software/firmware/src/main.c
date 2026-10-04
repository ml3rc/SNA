#include "frequency_limits.h"

#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "tusb.h"

#include "sna.h"

#include "eeprom.h"
#include "main.h"
#include "protocol.h"
#include "rf_control.h"
#ifdef SNA_MULTICORE
#include "acquisition.h"
#endif

#define LINE_BUF_SIZE 8192

static char line_buf[LINE_BUF_SIZE];
static size_t line_len = 0;
static bool line_overflow = false;

// Board-level GPIOs that aren't part of any chip driver in src/hal/ --
// everything RF-chip-specific (attenuator, switches, MAX2871, detector,
// amp, EEPROM/I2C, ADC) is initialized by its own HAL module instead, via
// rf_control_init() / eeprom_init() below. This is just the front-panel
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

    // GPIO_LED_PORT1_OUT_EN / GPIO_LED_PORT_Z50 are dual-purpose: besides
    // sourcing their status LED through a 71.5ohm resistor (like every
    // other status LED here), they also gate-drive a BSS84 P-channel
    // MOSFET (Q202/Q203) through a 1k/10k divider to the same 3.3V rail.
    // Measured real LED current (~13mA, from a ~3.06V HIGH level vs a
    // ~2.1V green LED Vf) exceeds the RP2350 pad's 4mA reset-default drive
    // strength and sags the output -- bumped to the max 12mA at the user's
    // request, 2026-09-30, after they measured this directly on hardware.
    gpio_set_drive_strength(GPIO_LED_PORT1_OUT_EN, GPIO_DRIVE_STRENGTH_12MA);
    gpio_set_drive_strength(GPIO_LED_PORT_Z50, GPIO_DRIVE_STRENGTH_12MA);

    // SDI self-test button (active-low, pulled up)
    gpio_init(GPIO_SDI_TEST_BUTTON);
    gpio_set_dir(GPIO_SDI_TEST_BUTTON, GPIO_IN);
    gpio_pull_up(GPIO_SDI_TEST_BUTTON);
}

static bool boot_self_test_ok = false; // set once in main(), read back below whenever the demo sweep isn't overriding the SDI pass/fail LEDs

// --- Button-triggered demo sweep ------------------------------------------
//
// Supersedes the old power_test_toggle() (a static single-frequency,
// single-coupler-position current/lock-detect check) now that lock-detect,
// the real `sweep` path, and the MAX2871 temperature sensor are all
// confirmed working on real hardware -- per the user's request, 2026-10-01:
// a press now starts a slow (~10s/sweep) frequency sweep across the whole
// MAX2871 range, and every time one sweep finishes, the coupler bank
// advances to the next of its 4 positions (port1 fwd/bwd, port2 fwd/bwd),
// cycling forever until pressed again. This is a hardware exercise/demo,
// not a data-collection tool -- the real `sweep` USB command already owns
// actual S-parameter measurement+calibration; this just visits every
// frequency on every physical path so the whole RF chain can be watched
// (bench equipment, LEDs, touch-temperature) without a PC connected.
//
// Deliberately a non-blocking state machine stepped from the main loop
// (see demo_sweep_step() below), NOT a blocking for-loop with sleep_ms()
// per point -- a single press can keep this running indefinitely (until
// pressed again), and a blocking implementation would stall tud_task()/USB
// line processing for that whole time, killing the USB link for as long as
// the demo runs.
#define DEMO_SWEEP_POINTS 64
#define DEMO_SWEEP_MS     10000u // ~10s/sweep, per the user's request
#define DEMO_POINT_DELAY_MS (DEMO_SWEEP_MS / DEMO_SWEEP_POINTS)
#define DEMO_F0_HZ SNA_FREQ_MIN_HZ   // Supported analyzer range
#define DEMO_F1_HZ SNA_FREQ_MAX_HZ

static const rf_coupler_path_t demo_path_order[4] = {
    RF_COUPLER_PORT1_FWD, RF_COUPLER_PORT1_BWD, RF_COUPLER_PORT2_FWD, RF_COUPLER_PORT2_BWD,
};

static bool demo_active = false;
static int demo_path_idx = 0;
static uint32_t demo_point_idx = 0;
static uint32_t demo_last_point_ms = 0;

// The demo excites the port whose coupler path it is exercising. This is
// a hardware exercise, not a transmission measurement.
static uint8_t port_for_demo_path(rf_coupler_path_t path) {
    return (path == RF_COUPLER_PORT2_FWD || path == RF_COUPLER_PORT2_BWD) ? 2 : 1;
}

static void demo_select_path(rf_coupler_path_t path) {
    rf_set_port(port_for_demo_path(path));
    rf_select_coupler_path(path);
}

static void demo_sweep_toggle(void) {
    demo_active = !demo_active;
    if (demo_active) {
        demo_path_idx = 0;
        demo_point_idx = 0;
        rf_set_atten(64); // mid-range, not the safe-startup max(127)/min(0) extreme -- same pick the old power_test_toggle() used
        // rf_measure_session_start() enables amp/detector/source for the
        // current z, including the datasheet's double-write/lock-wait dance
        // -- same lifecycle every USB `sweep` uses, see rf_control.c.
        if (!rf_measure_session_start(rf_get_z(), DEMO_F0_HZ)) {
            demo_active = false;
            return;
        }
        demo_select_path(demo_path_order[demo_path_idx]);
        demo_last_point_ms = to_ms_since_boot(get_absolute_time());
    } else {
        rf_measure_session_end();
        gpio_put(GPIO_SDI_TEST_PASS_LED, boot_self_test_ok);
        gpio_put(GPIO_SDI_TEST_FAIL_LED, !boot_self_test_ok);
    }
}

// Does at most one sweep point's worth of work per call -- cheap enough
// (same retune+read cost as a real `sweep` point, already measured at
// ~2.5ms/point) to call unconditionally every main-loop iteration; it just
// no-ops until DEMO_POINT_DELAY_MS has actually elapsed.
static void demo_sweep_step(uint32_t now_ms) {
    if (!demo_active) return;
    if (now_ms - demo_last_point_ms < DEMO_POINT_DELAY_MS) return;
    demo_last_point_ms = now_ms;

    double frac = (double) demo_point_idx / (double) (DEMO_SWEEP_POINTS - 1);
    uint64_t f = (uint64_t) ((double) DEMO_F0_HZ + frac * (double) (DEMO_F1_HZ - DEMO_F0_HZ));

    // Harmonic-suppression LPF pick -- same threshold ladder as
    // rf_control.c's rf_measure_point(), duplicated rather than shared
    // since main.c already composes RF-control calls directly for the
    // button (same pattern the old power_test_toggle() used).
    if (f <= 947000000ULL) rf_set_lpf(3);
    else if (f <= 1747000000ULL) rf_set_lpf(2);
    else if (f <= 3500000000ULL) rf_set_lpf(1);
    else rf_set_lpf(0);

    rf_max2871_set_freq(f);
    (void) rf_detector_read_raw(); // exercises the detector path; nothing consumes the value here -- see the comment above this section

    demo_point_idx++;
    if (demo_point_idx >= DEMO_SWEEP_POINTS) {
        demo_point_idx = 0;
        demo_path_idx = (demo_path_idx + 1) % 4;
        demo_select_path(demo_path_order[demo_path_idx]);
    }
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

// Updates the 3 status LEDs that have a real, live meaning (as opposed to
// GPIO_LED_BOOTED's own heartbeat, see main()) -- called every main-loop
// iteration. GPIO writes happen only when needed, outside ADC acquisition:
//   READY/BUSY: a sweep is in progress, whether from the USB `sweep`
//   command (protocol_is_sweeping()) or the button's own demo-sweep mode.
//   PORT1_OUT_EN: the source is currently driving port1 (rf_get_port()),
//   not port2.
//   PORT_Z50: current impedance mode is 50ohm, not 75ohm -- NOTE
//   boards/sna.h flags this pin's polarity as unverified against the
//   75ohm indicator; check on the bench once flashed (toggle z, confirm
//   the LED actually tracks the mode you'd expect) rather than trusting
//   this blindly.
void update_status_leds(void) {
    bool busy = demo_active || protocol_is_sweeping();
    // Only change these at session boundaries. Core 1 may already be
    // sampling the next point while core 0 handles the previous point.
    if (gpio_get(GPIO_LED_BUSY) != busy) gpio_put(GPIO_LED_BUSY, busy);
    if (gpio_get(GPIO_LED_READY) != !busy) gpio_put(GPIO_LED_READY, !busy);
    // During acquisition the hardware owner updates port/Z LEDs along with
    // routing, BEFORE the settling delay. Core 0 must not write them here:
    // a stale port snapshot could otherwise create a pulse during ADC reads.
    if (!busy) {
        if (gpio_get(GPIO_LED_PORT1_OUT_EN) != (rf_get_port() == 1))
            gpio_put(GPIO_LED_PORT1_OUT_EN, rf_get_port() == 1);
        if (gpio_get(GPIO_LED_PORT_Z50) != (rf_get_z() == 50))
            gpio_put(GPIO_LED_PORT_Z50, rf_get_z() == 50);
    }

    // Deliberately NOT showing live lock-detect on the SDI pass/fail LEDs
    // during the demo sweep (an earlier version of this did) -- confirmed
    // on real hardware, 2026-10-01, that it looks like erratic/broken
    // blinking rather than a useful indicator: the demo jumps ~95MHz
    // between points (64 points across the MAX2871's whole 50MHz-6000MHz
    // range, not the small nearby-frequency retunes the fast sweep path is
    // tuned for), so the chip genuinely drops and reacquires lock after
    // most points with no settle wait in demo_sweep_step() to smooth that
    // over. Simplest fix: don't surface it here at all -- these LEDs just
    // keep showing the boot self-test result throughout, same as idle.
}

void stop_demo_sweep(void) {
    if (!demo_active) return;
    demo_active = false;
    rf_measure_session_end();
    update_status_leds();
}

// Core 0 services this from both idle and blocking protocol/acquisition paths.
void update_heartbeat(void) {
    static bool initialized = false;
    static uint32_t start_ms;
    static bool led_state;
    uint32_t now_ms = to_ms_since_boot(get_absolute_time());
    if (!initialized) { start_ms = now_ms; initialized = true; }
    uint32_t elapsed = now_ms - start_ms;
    start_ms += (elapsed / 1000) * 1000;
    bool on = (elapsed % 1000) < 100;
    if (on != led_state) { led_state = on; gpio_put(GPIO_LED_BOOTED, on); }
}

void poll_usb_commands(void) {
    update_heartbeat();
    if (!tud_cdc_connected()) {
        line_len = 0;
        line_overflow = false;
        return;
    }
    // Bound work per poll so a stream of host commands cannot starve RF work.
    uint32_t budget = LINE_BUF_SIZE;
    while (budget-- > 0 && tud_cdc_available()) {
        char c;
        if (tud_cdc_read(&c, 1) == 0) break;
        if (c == '\n') {
            line_buf[line_len] = '\0';
            bool dispatch = !line_overflow && line_len > 0;
            // Reset BEFORE dispatch: a blocking sweep polls this same reader.
            line_len = 0;
            line_overflow = false;
            if (dispatch) protocol_handle_line(line_buf);
        } else if (c != '\r' && !line_overflow) {
            if (line_len < LINE_BUF_SIZE - 1) line_buf[line_len++] = c;
            else line_overflow = true; // discard the ENTIRE line, including its tail
        }
    }
}

int main(void) {
    board_gpio_init();
    eeprom_init();
    bool eeprom_ok = eeprom_self_test();
    rf_control_init();
    bool detector_ok = rf_detector_self_test();
    protocol_init();
    tusb_init();
    // SWD reset can be too brief for the host to notice the old USB device
    // disappeared. Force a visible detach before enumerating a fresh session.
    tud_disconnect();
    sleep_ms(250);
    tud_connect();

    // Combined bring-up result, held steady on the SDI pass/fail LEDs (both
    // GPIO paths were already confirmed working directly by the user in the
    // prior session, so this no longer needs the "light both together"
    // workaround that used to be here). Cached in boot_self_test_ok so
    // demo_sweep_toggle() can restore this display when the demo sweep
    // stops (a real SDI self-test, protocol_run_sdi_self_test(), overlays
    // these LEDs independently and is unrelated to the demo sweep).
    boot_self_test_ok = eeprom_ok && detector_ok;
    gpio_put(GPIO_SDI_TEST_PASS_LED, boot_self_test_ok);
    gpio_put(GPIO_SDI_TEST_FAIL_LED, !boot_self_test_ok);

    // One 100ms heartbeat pulse per second on BOOTED -- proves board_gpio_init()/
    // rf_control_init() (and everything under it in src/hal/) ran without
    // hanging AND that the main loop below is still alive, not just that
    // init ran once. Deliberately kept at a fixed cadence (not an
    // "activity" indicator that blinks faster when busy) so a hung main
    // loop is visibly dead (stuck on or off) rather than possibly stuck
    // solid-on and looking fine at a glance -- the other 3 status LEDs
    // (READY/BUSY/PORT1_OUT_EN/PORT_Z50, see update_status_leds()) already
    // cover live activity/state separately.
#ifdef SNA_MULTICORE
    acquisition_init();
#endif
    update_heartbeat();

    debounced_button_t button = { .raw_prev = true, .stable = true, .last_change_ms = 0 };

    while (1) {
        tud_task();

        poll_usb_commands();

        uint32_t now_ms = to_ms_since_boot(get_absolute_time());

        if (button_press_edge(&button, gpio_get(GPIO_SDI_TEST_BUTTON), now_ms)) {
            static sdi_test_point_t sdi_probe[EEPROM_SDI_POINTS_MAX];
            size_t sdi_count;
            eeprom_sdi_test_get(sdi_probe, &sdi_count);
            if (sdi_count > 0) {
                protocol_run_sdi_self_test();
            } else {
                demo_sweep_toggle(); // see the comment above this function
            }
        }

        demo_sweep_step(now_ms);
        update_status_leds();

        // 100ms pulse every second -- see the comment above the initial
        // gpio_put() block for what this proves.
        update_heartbeat();
    }

    return 0;
}
