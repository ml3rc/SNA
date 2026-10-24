# Changelog

## v1.0.0 — 2026-10-24

First release. Builds (firmware `.uf2`, Windows installer, Android APK) are attached to the
[GitHub release](https://github.com/ml3rc/SNA/releases).

| Part | Version | Compatibility |
|---|---|---|
| Firmware | 1.0.0 | protocol 3, EEPROM format 3 |
| Desktop client | 1.0.0 | protocol 3 |
| Android client | 1.0.0 (versionCode 11) | protocol 3 |
| PCB | 1.0 | |

### Hardware

- 2-port scalar network analyzer, 50 MHz – 6 GHz, 50 Ω (SMA) and 75 Ω (BNC) per port.
- 4-layer PCB: RP2350B, MAX2871 synthesizer, LPF bank (947 / 1747 / 3500 MHz + bypass), HMC1119 attenuator,
  TRF37A73 amplifier, one SEDC-10-63+ coupler behind six SP4T switches, AD8318 log detector, 50 → 75 Ω pads.
- USB-C for power and host link, 24AA32A EEPROM, SWD header, status/port/impedance LEDs, SDI test button.
- Milled aluminium case.

### Firmware

- JSON lines over USB CDC-ACM (protocol 3).
- Modes entered and left by the host: sweep, generator, power meter, manual control. Impedance and output level
  can be changed live.
- Sweeps of 1–4096 points, linear or log, S11/S21/S12/S22 or raw coupler paths; ≈ 2.5 s for 1001 points.
- SLOT calibration (Open/Short per port, Thru) stored in the EEPROM and applied on the device, 8 sets
  (port × direction × impedance).
- CW generator, single and continuous power meter, detector probe, synthesizer temperature readout.
- On-board SDI cable test with the button, works without a PC.
- Dual core: core 1 owns all RF hardware, core 0 handles USB, protocol, calibration and EEPROM.
- Boot self-test with blink codes, watchdog, bounded lock waits.
- Update over USB: `bootsel` command or BOOTSEL button, then copy the `.uf2`.

### Client

- Kotlin/Compose Multiplatform: Windows/Linux/macOS desktop and Android (USB OTG).
- Sweep: merged or separate charts, hover values, min/max markers, reference overlay or difference, loop sweeps,
  live output level, Touchstone (`.s1p`/`.s2p`), JSON and CSV export, raw detector view.
- Generator with detector probe, power meter.
- Calibration wizard per impedance (capture, compute, upload, verify).
- SDI self-test thresholds: load, edit, upload; last on-board result.
- Diagnostics: device info, live status, boot errors, manual RF control, reboot into the bootloader.
- Console with the full traffic log; JSON commands can be typed and sent.
- Interlock: one tool drives the RF hardware at a time, controls that cannot reach the hardware are locked.
- Windows MSI installer that upgrades in place; desktop memory around 300 MB.

### Documentation

- Diagram-first docs for API, firmware and hardware; protocol, pin map and EEPROM layout specs.
- Measurements (output signals, LibreVNA comparison) and photos.
- Presentation diagrams and animations, incl. RF basics (transmission lines, reflections, S-parameters).
