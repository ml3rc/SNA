// Block layer: AD8318 detector.
#include "rf_control/detector.h"

#include "hal/ad8318.h"

// The median below averages the two middle codes of an even-sized batch.
_Static_assert(RF_CTRL_DETECTOR_SAMPLES >= 2 && RF_CTRL_DETECTOR_SAMPLES % 2 == 0,
               "RF_CTRL_DETECTOR_SAMPLES must be even and at least 2");

void rf_ctrl_detector_init(void) {
    hal_ad8318_init();
    hal_ad8318_enable(true);
}

uint16_t rf_ctrl_detector_read_raw(void) {
    return hal_ad8318_read_raw();
}

rf_ctrl_detector_sample_t rf_ctrl_detector_sample(void) {
    uint16_t samples[RF_CTRL_DETECTOR_SAMPLES];
    for (unsigned i = 0; i < RF_CTRL_DETECTOR_SAMPLES; ++i) {
        uint16_t value = rf_ctrl_detector_read_raw();
        unsigned j = i;
        while (j > 0 && samples[j - 1] > value) {
            samples[j] = samples[j - 1];
            --j;
        }
        samples[j] = value;
    }
    rf_ctrl_detector_sample_t batch;
    // Even sample count: round the average of the two middle ADC codes.
    batch.median = (uint16_t)(((uint32_t)samples[RF_CTRL_DETECTOR_SAMPLES / 2u - 1u] +
                               samples[RF_CTRL_DETECTOR_SAMPLES / 2u] + 1u) / 2u);
    batch.min = samples[0];
    batch.max = samples[RF_CTRL_DETECTOR_SAMPLES - 1u];
    return batch;
}
