# Firmware

RP2350B (2 × Cortex-M33), C11, Pico SDK 2.2.0, TinyUSB, bare metal (no RTOS). Version 0.4.x.

| # | Page | Slide topic |
|---|---|---|
| 1 | [architecture.md](architecture.md) | Dual core split, layers, core link |
| 2 | [rf-chain.md](rf-chain.md) | RF signal path, what firmware controls, source safety |
| 3 | [modes.md](modes.md) | Operating modes, request handling |
| 4 | [sweep.md](sweep.md) | Sweep end-to-end, one point, timing |
| 5 | [calibration.md](calibration.md) | Calibration flow, EEPROM |
| 6 | [boot-and-leds.md](boot-and-leds.md) | Boot self-test, LEDs, SDI test button, watchdog |

Specs: [protocol/PROTOCOL.md](protocol/PROTOCOL.md) · [protocol/PINMAP.md](protocol/PINMAP.md) ·
[protocol/EEPROM_LAYOUT.md](protocol/EEPROM_LAYOUT.md) · API overview: [../api/README.md](../api/README.md)

## Key numbers

| | |
|---|---|
| Frequency | 50 MHz – 6 GHz (sweep grid), up to 4096 points, lin / log |
| Speed | 1001-point sweep ≈ 2.5 s |
| Level | 128 steps × 0.25 dB |
| Calibration | 8 curves (S11/S21/S12/S22 × 50/75 Ω), 240 points each, in 4 KB EEPROM |
| Detector | AD8318 log detector, 16-sample median |
