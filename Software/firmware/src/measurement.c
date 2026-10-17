#include "measurement.h"

#include "eeprom.h"

_Static_assert(MEASUREMENT_SPARAMS == EEPROM_SPARAMS, "calibration sets and SDI points use the same S-parameter order");

const char *const MEASUREMENT_SPARAM_NAMES[MEASUREMENT_SPARAMS] = {
    [MEASUREMENT_IDX_S11] = "s11", [MEASUREMENT_IDX_S21] = "s21",
    [MEASUREMENT_IDX_S12] = "s12", [MEASUREMENT_IDX_S22] = "s22",
};

measurement_t measurement_process(const rf_app_raw_measurement_t *raw, uint64_t freq_hz, uint8_t z,
                                  uint8_t sparams) {
    measurement_t m = {0};
    if (!raw->measured) return m;
    m.locked = raw->valid;

    // Detector slope and sign convention: see rf_app_counts_ratio_db(). Sij is
    // read with source j: reflection on its own port, transmission on the other.
    const uint16_t numerator[MEASUREMENT_SPARAMS] = {
        [MEASUREMENT_IDX_S11] = raw->source1_reflection, [MEASUREMENT_IDX_S21] = raw->source1_transmission,
        [MEASUREMENT_IDX_S12] = raw->source2_transmission, [MEASUREMENT_IDX_S22] = raw->source2_reflection,
    };
    const uint16_t reference[MEASUREMENT_SPARAMS] = {
        [MEASUREMENT_IDX_S11] = raw->source1_ref, [MEASUREMENT_IDX_S21] = raw->source1_ref,
        [MEASUREMENT_IDX_S12] = raw->source2_ref, [MEASUREMENT_IDX_S22] = raw->source2_ref,
    };

    // EEPROM SLOT calibration: each of the 8 stored curves (s11_50/s21_50/
    // .../s22_75) is ALREADY the final correction value to subtract at a
    // given frequency -- what that curve actually contains (avg(open,short)
    // for reflection tracking, or the thru reading for transmission
    // tracking -- the standard scalar-NA techniques) is a client-
    // side cal-wizard concern; this function doesn't need to know how the
    // curve was derived, only that subtracting it removes the system's own
    // frequency response from the raw reading. `calibrated` is only true if
    // EVERY requested parameter had a stored curve covering this freq_hz --
    // if even one didn't, say false rather than imply the whole reading is
    // trustworthy when part of it is still raw.
    bool all_calibrated = sparams != 0;
    for (int i = 0; i < MEASUREMENT_SPARAMS; i++) {
        if (!(sparams & (1u << i))) continue;
        m.db[i] = rf_app_counts_ratio_db(numerator[i], reference[i]);
        double correction;
        if (eeprom_interpolate_cached(eeprom_cal_set_index(z, i), freq_hz, &correction)) m.db[i] -= correction;
        else all_calibrated = false;
    }
    m.calibrated = all_calibrated && m.locked;
    return m;
}
