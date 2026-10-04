#ifndef SNA_PROTOCOL_H
#define SNA_PROTOCOL_H

#include <stdbool.h>

// Command dispatch implementing Software/protocol/PROTOCOL.md. `eeprom.h` is
// included for sdi_test_point_t / EEPROM_SDI_POINTS_MAX, shared between the
// wire protocol and on-device storage.
#include "eeprom.h"

void protocol_init(void);

// True while a USB sweep or a front-panel SDI measurement is in progress -- lets
// main.c's BUSY/READY status LEDs reflect USB-driven sweeps too, not just
// the front-panel button's own demo-sweep mode.
bool protocol_is_sweeping(void);
// RF lock waits use this to service Abort/disconnect during long profiling waits.
bool protocol_measurement_cancelled(void);

// Parses one line (a single JSON object, NUL-terminated, no trailing '\n')
// and writes its response(s) directly to the USB CDC connection.
void protocol_handle_line(const char *line);

// Runs the standalone front-panel SDI test: sweeps the stored threshold
// points, blinks the pass/fail LEDs in sequence, and -- if a PC happens to
// be connected -- also emits an unsolicited "sdi_test_result" event.
// Called from the main loop on a button-press edge.
void protocol_run_sdi_self_test(void);

#endif
