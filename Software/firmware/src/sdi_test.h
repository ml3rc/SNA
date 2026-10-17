#ifndef SNA_SDI_TEST_H
#define SNA_SDI_TEST_H

#include <stdbool.h>
#include <stdint.h>

#include "rf_task.h"

// The standalone on-board SDI test (PROTOCOL.md "LEDs and SDI button") on core 0:
// enters the sweep mode, sweeps the stored threshold points at attenuation 0,
// leaves the mode, sends `sdi_test_result` if a host is connected and blinks
// one green/red pulse per point. Non-blocking: driven by rf_task replies and
// sdi_test_update_leds() from the main loop.

// Button press. Does nothing while a test runs; one short orange blink (green
// and red) without stored points; refused (one short red blink) unless the RF
// side is idle.
void sdi_test_start(void);
// From the press until the mode is left again (host commands get `busy`).
bool sdi_test_running(void);
// Abort / mode exit from the host: a test still entering its mode does not
// start its sweep (a running sweep is ended through rf_task_cancel()).
void sdi_test_cancel(void);
// Takes the replies to the test's own requests; false for any other reply.
bool sdi_test_handle_reply(const rf_reply_t *reply);
// The result blink sequence; called from the main loop.
void sdi_test_update_leds(uint32_t now_ms);

#endif
