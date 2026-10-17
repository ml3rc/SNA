// Block layer: directional coupler bank.
#include "rf_control/coupler.h"

#include "hal/rf_switch.h"
#include "sna.h"

// Directional coupler bank: 6 independently-addressed SP4T switches. Index
// <-> schematic designator, per Altium/SNAPCBVersion-INT's "RF Directional
// Coupler" sheet (GPIO_SW_CPLRn is named after the net label on that sheet,
// which matches these designators 1:1): 0=U502, 1=U501, 2=U503, 3=U504,
// 4=U506, 5=U505. See rf_ctrl_coupler_select() below for the routing matrix
// driving these together (coupler1..6 in that function's table = this
// array's index 0..5, same order).
#define CPLR_SWITCHES 6
static const hal_rf_switch_t sw_cplr[CPLR_SWITCHES] = {
    { GPIO_SW_CPLR1_C1, GPIO_SW_CPLR1_C2 },
    { GPIO_SW_CPLR2_C1, GPIO_SW_CPLR2_C2 },
    { GPIO_SW_CPLR3_C1, GPIO_SW_CPLR3_C2 },
    { GPIO_SW_CPLR4_C1, GPIO_SW_CPLR4_C2 },
    { GPIO_SW_CPLR5_C1, GPIO_SW_CPLR5_C2 },
    { GPIO_SW_CPLR6_C1, GPIO_SW_CPLR6_C2 },
};

void rf_ctrl_coupler_init(void) {
    for (int i = 0; i < CPLR_SWITCHES; i++) hal_rf_switch_init(&sw_cplr[i]);
}

// Coupler-bank routing matrix as designed, indexed by port + direction.
// All four legs of each switch are used across this table; the 50-ohm
// resistors at the switches (R510/R509/R530/R529 etc.) are there for
// protection, not as an indication that a leg is unused.
void rf_ctrl_coupler_select(rf_ctrl_coupler_path_t path) {
    // Row order matches rf_ctrl_coupler_path_t; column order matches sw_cplr[]
    // (coupler1..6 = U502,U501,U503,U504,U506,U505, see the comment above
    // that array).
    // A path without a row would zero-fill to HAL_RF_LEG_4 everywhere: a silent misroute.
    static const uint8_t table[][CPLR_SWITCHES] = {
        [RF_CTRL_COUPLER_PORT1_FWD] = { HAL_RF_LEG_2, HAL_RF_LEG_4, HAL_RF_LEG_3, HAL_RF_LEG_2, HAL_RF_LEG_4, HAL_RF_LEG_1 },
        [RF_CTRL_COUPLER_PORT1_BWD] = { HAL_RF_LEG_4, HAL_RF_LEG_4, HAL_RF_LEG_4, HAL_RF_LEG_1, HAL_RF_LEG_2, HAL_RF_LEG_1 },
        [RF_CTRL_COUPLER_PORT2_FWD] = { HAL_RF_LEG_1, HAL_RF_LEG_2, HAL_RF_LEG_1, HAL_RF_LEG_3, HAL_RF_LEG_3, HAL_RF_LEG_2 },
        [RF_CTRL_COUPLER_PORT2_BWD] = { HAL_RF_LEG_1, HAL_RF_LEG_1, HAL_RF_LEG_2, HAL_RF_LEG_4, HAL_RF_LEG_3, HAL_RF_LEG_4 },
    };
    _Static_assert(sizeof(table) / sizeof(table[0]) == RF_CTRL_COUPLER_PATH_COUNT, "a table row per coupler path");
    if ((unsigned) path >= (unsigned) RF_CTRL_COUPLER_PATH_COUNT) return;
    for (int i = 0; i < CPLR_SWITCHES; i++) {
        hal_rf_switch_set(&sw_cplr[i], table[path][i]);
    }
}
