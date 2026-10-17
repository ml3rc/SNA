#ifndef SNA_PROTOCOL_H
#define SNA_PROTOCOL_H

#include <stdbool.h>

#include "cJSON.h"

// The host protocol (Software/docs/firmware/protocol/PROTOCOL.md) on core 0. Commands are
// validated here and turned into rf_task requests; their replies come back
// asynchronously and are answered from protocol_poll(), so USB keeps being
// serviced while core 1 works.

// True while a USB sweep or the on-board SDI test runs (BUSY LED, `busy`).
bool protocol_is_sweeping(void);

// The mode in core 1's latest final reply is idle (it may be changing right now:
// core 1 still decides).
bool protocol_rf_idle(void);

// Parses one line (a single JSON object, NUL-terminated, no trailing '\n')
// and answers it, now or once core 1 has replied.
void protocol_handle_line(const char *line);

// Answers a line that was too long to read (its id is lost): bad_request.
void protocol_reject_line(void);

// Main loop work: the replies from core 1 (also for the SDI test), the
// continuous power meter's events, and cancelling what a vanished host left
// running. True if anything was done.
bool protocol_poll(void);

// Writes an unsolicited event (no "id") to the host, if one is connected; frees `evt`.
void protocol_send_event(cJSON *evt);

// The USB host went away (main.c, on the connected -> disconnected edge): the next
// session starts with a clean output line.
void protocol_host_disconnected(void);

#endif
