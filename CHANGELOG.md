# Changelog

## v1.0.0 — 2026-10-20

First release. All parts restart their numbering at 1.0.0 (earlier internal builds: firmware 0.1–0.4.1, desktop
client up to 1.5, Android client up to 0.4).

| Part | Version | Compatibility |
|---|---|---|
| Firmware | 1.0.0 | protocol 3, EEPROM format 3 (same as firmware 0.4.x, no recalibration needed) |
| Desktop client | 1.0.0 | protocol 3 |
| Android client | 1.0.0 (versionCode 11) | protocol 3 |
| PCB | 1.0 | Q202/Q203 footprint erratum, see README |

### Hardware

- 4-layer PCB rev 1.0 (RP2350B, MAX2871, LPF bank 947 / 1747 / 3500 MHz + bypass, HMC1119, TRF37A73, one
  SEDC-10-63+ coupler behind six SP4T switches, AD8318, 50 → 75 Ω pads), milled aluminium case.

### Firmware

- Dual core: core 1 owns all RF hardware, core 0 does USB, JSON protocol, dB conversion, calibration and EEPROM.
- Modes (sweep, generator, power meter, manual) entered and left by the host; settings may change live.
- Sweeps of 1–4096 points, linear or log, S11/S21/S12/S22 or raw coupler paths; ≈ 2.5 s for 1001 points.
- SLOT calibration (Open/Short per port, Thru) in the EEPROM, 8 sets (port × direction × impedance).
- On-board SDI cable test with the button, works without a PC.
- Watchdog, bounded lock waits, boot self-test blink codes.

### Client

- Kotlin/Compose Multiplatform, Windows/Linux/macOS desktop and Android.
- Tabs: Sweep, Generator, Power meter, Calibration wizard, Self-test, Diagnostics, Console.
- New look: warm dark theme with copper and violet accents, traces in network-analyzer colours (S11 yellow, S21
  cyan, S12 magenta, S22 green), notch logo as app icon.
- Windows MSI upgrades in place (stable upgrade code, build tick in the MSI version).

### Documentation

- `docs/` moved from `Software/docs/` to the repository root; measurements and photos now live in
  `docs/measurements/` and `docs/pictures/`.
- New RF basics animations for the presentation: TransmissionLine, Reflections, SParameters (EN + DE).
- Diagrams, protocol and READMEs updated to the 1.0.0 versions.

### Upgrading from internal builds

- Desktop: a 1.0.0 MSI is older than the 1.5.x test builds, so uninstall those once; later 1.0.x builds upgrade
  in place.
- Firmware: copy `sna_firmware.uf2` onto the BOOTSEL drive. Calibration from firmware 0.4.x stays valid.
