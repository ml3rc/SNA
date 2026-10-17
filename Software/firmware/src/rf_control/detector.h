#ifndef SNA_RF_CONTROL_DETECTOR_H
#define SNA_RF_CONTROL_DETECTOR_H

#include <stdbool.h>
#include <stdint.h>

// Block layer: AD8318 log detector on the coupler bank's COUPLED output.

#define RF_CTRL_DETECTOR_SAMPLES 16u
#define RF_CTRL_DETECTOR_SETTLE_US 100u // after a coupler-path/port switch, before sampling

// Powers the detector up; it stays on from boot.
void rf_ctrl_detector_init(void);

uint16_t rf_ctrl_detector_read_raw(void); // 12-bit ADC counts

// RF_CTRL_DETECTOR_SAMPLES readings: the median, and min/max as the spread
// within the batch (noise, instability).
typedef struct { uint16_t median, min, max; } rf_ctrl_detector_sample_t;
rf_ctrl_detector_sample_t rf_ctrl_detector_sample(void);
// (Converting codes to dB/dBm is application processing: rf_app_counts_*().)

#endif
