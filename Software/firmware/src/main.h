#ifndef SNA_MAIN_H
#define SNA_MAIN_H

// Refreshes the BUSY/READY/PORT1_OUT_EN/PORT_Z50 status LEDs from current
// state. Called from the main loop, on measurement transitions, and directly
// by rf_set_port()/rf_set_z() so intermediate route changes are visible.
void update_status_leds(void);
void update_heartbeat(void);

// Shared CDC reader. Sweep code polls it between points so Abort is serviced.
void poll_usb_commands(void);
// Relinquish the front-panel demo before a protocol measurement takes over.
void stop_demo_sweep(void);

#endif
