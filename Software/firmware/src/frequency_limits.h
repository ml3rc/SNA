#ifndef SNA_FREQUENCY_LIMITS_H
#define SNA_FREQUENCY_LIMITS_H

// The supported band. Also in: the client's FrequencyLimits.kt, PROTOCOL.md
// and the EEPROM validity rules (EEPROM_LAYOUT.md); stored calibration grids
// and SDI points outside it read as empty, so change them together.
#define SNA_FREQ_MIN_HZ 50000000ULL
#define SNA_FREQ_MAX_HZ 6000000000ULL
#define SNA_FREQ_DEFAULT 1000000000ULL

#endif
