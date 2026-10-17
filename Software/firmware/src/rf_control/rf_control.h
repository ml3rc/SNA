#ifndef SNA_RF_CONTROL_H
#define SNA_RF_CONTROL_H

// Board-level RF control, built on the per-chip HAL modules in src/hal/ in
// three layers; each layer calls only the one directly below it:
//   application  application.h  rf_app_*   state machine IDLE/SWEEP/GENERATOR/
//                                          PWR_DETECTOR/MANUAL, measurements
//   state        state.h        rf_state_* current hardware configuration and the
//                                          safe order of every hardware change
//   block        source.h, coupler.h,      rf_ctrl_*  one RF block each, the only
//                detector.h, ports.h                  HAL callers
//   hal          src/hal/*.h    hal_*      one chip each
// Code outside src/rf_control/ includes this header and uses rf_app_* only.
// The application layer depends on nothing above it: cancellation comes in
// through rf_app_hooks_t. It runs on core 1 only (rf_task.c).

#include "rf_control/application.h"

#endif
