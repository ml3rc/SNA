#ifndef SNA_MAIN_H
#define SNA_MAIN_H

#include <stdbool.h>
#include <stdint.h>

// Boot checks. After a failure BOOTED stays off and the red SDI LED repeats
// the code of every failed check: as many short blinks as the code's number.
// The bit order is also status.init_errors' (PROTOCOL.md).
typedef enum {
    INIT_ERR_EEPROM = 1u << 0,   // 1 blink: EEPROM not readable/writable (I2C)
    INIT_ERR_SOURCE = 1u << 1,   // 2 blinks: MAX2871 did not lock at SNA_FREQ_DEFAULT
    INIT_ERR_DETECTOR = 1u << 2, // 3 blinks: parked detector reading implausible
    INIT_ERR_USB = 1u << 3,      // 4 blinks: TinyUSB init failed
} init_error_t;
#define INIT_ERROR_CODES 4 // the number of init_error_t bits
// init_error_t bits of the failed boot checks, 0 after a good boot.
uint8_t get_init_errors(void);

// BUSY/READY show the load of both cores (linear PWM brightness, updated
// every LOAD_WINDOW_US = 1 ms); called from the main loop. PORT1_OUT_EN/PORT_Z50 follow the port
// and impedance switches directly (rf_control/ports.c).
void update_status_leds(void);
// BOOTED heartbeat (or the init error code); called wherever core 0 waits, so it keeps blinking.
void update_heartbeat(void);
// The on-board SDI test result LEDs (green pass, red fail); writes only on a change.
void set_sdi_leds(bool pass, bool fail);

// The CDC line reader: hands every complete line to protocol_handle_line().
// True if it received anything.
bool poll_usb_commands(void);

#endif
