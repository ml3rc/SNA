#ifndef SNA_RF_CONTROL_COUPLER_H
#define SNA_RF_CONTROL_COUPLER_H

// Block layer: directional coupler bank (6 switches U501-U506 around FL501).

void rf_ctrl_coupler_init(void);

// Which of the 4 coupler-bank configurations is currently selected (see
// rf_ctrl_coupler_select() in coupler.c for the full switch matrix).
// "Forward"/"backward" refer
// to which traveling-wave direction through FL501 ends up sampled onto
// COUPLED (-> AD8318) for that port.
typedef enum {
    RF_CTRL_COUPLER_PORT1_FWD,
    RF_CTRL_COUPLER_PORT1_BWD,
    RF_CTRL_COUPLER_PORT2_FWD,
    RF_CTRL_COUPLER_PORT2_BWD,
    RF_CTRL_COUPLER_PATH_COUNT,
} rf_ctrl_coupler_path_t;

// (There is no raw coupler access: only the four configurations above exist.)
// Drives the 6-switch coupler bank into the given configuration (ignored for
// values outside the enum). Independent
// of port-select/impedance/filter, each its own separate stage.
void rf_ctrl_coupler_select(rf_ctrl_coupler_path_t path);

#endif
