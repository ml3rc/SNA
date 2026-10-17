# SNA client

Kotlin Multiplatform + Compose Multiplatform app for the SNA scalar network analyzer: desktop
(Windows/Linux/macOS on the JVM) and Android. No iOS target. It speaks the wire protocol in
[`../protocol/PROTOCOL.md`](../protocol/PROTOCOL.md) over USB CDC-ACM.

| | Version | Needs |
|---|---|---|
| Desktop | **1.4.0** | protocol **3** (firmware 0.4.x) |
| Android | **0.4.0** (versionCode 9) | protocol **3** (firmware 0.4.x) |

The client refuses firmware reporting any other `proto` (older firmware: update the firmware;
newer: update the client), and any frequency range other than 50 MHz–6 GHz.

## Features

The workspace has seven tabs. A green square on a tab means its tool is running on the device.

- **Sweep** — S11/S21/S12/S22 magnitude, 1–4096 points, **linear or logarithmic spacing**
  (the chart's X axis follows the grid, so a log sweep reads as a log frequency axis), 50/75 Ω,
  single or loop sweeps, relative output level (also changed live during a sweep, from the next
  point on). Merged or separate charts with hover values, min/max search markers, a held
  reference shown as overlay or difference, Touchstone (`.s1p`/`.s2p`) and JSON export. Points of a
  repeated sweep are updated in place; the part not yet re-measured is drawn faded.
  The **Raw detector** view sweeps the four coupler states (port 1/2 forward/backward) from either
  source port, as detector dBm or ADC counts, with CSV export.
- **Generator** — CW output: port, impedance, level, frequency. Level and impedance change a
  running output live; port and frequency through "Apply changes". The **detector probe** reads
  all four coupler paths under the running output and explains suspicious readings.
- **Power meter** — continuous reading of the signal arriving at a port (approximate,
  uncalibrated dBm; `docs/generator-and-power-meter.md`).
- **Calibration** — SLOT wizard per impedance: Open/Short at each port and Thru, compute, upload,
  verify. Detects and locks the device's existing grid (all 8 sets share one). Captures run in the
  background and can be aborted.
- **Self-test** — the front-panel SDI thresholds (load, edit, upload; an empty list disables the
  test) and the last front-panel result the device reported.
- **Diagnostics** — device info, tool states, live `status`, boot self-test errors, synthesizer
  temperature; **manual RF control** (output port/termination, source on/off, LPF, coupler path,
  synthesizer frequency, one-shot detector readout); reboot into the UF2 bootloader.
- **Console** — every line sent and received, with timestamps (background polling and sweep
  points hidden unless switched on; the log keeps the newest 3000 lines). Lines can be typed and
  sent while no test runs; a JSON object without `"id"` gets `"id":"console-N"`, which never
  collides with the client's own integer ids. Up/Down recall earlier lines.

### Interlock and locked controls

Only one tool drives the RF hardware at a time (`session/ToolActivity.kt`, `blockReason`). A tool
that would take over another is refused with the reason shown, and the device's own `status`
reveals what the client did not start (the front-panel test, a mode entered from the console).

While a test runs, controls that would not reach the hardware are disabled:

- the running tool's settings that only apply when it starts (sweep range, points, spacing,
  impedance, parameters, calibration grid, ...);
- every input of the tools that are locked out, the console prompt included.

Controls that act on the running test stay enabled: the output level during sweeps and generator
output, the generator's impedance/port/frequency, the power meter's port and impedance, Abort and
Stop, manual control while it is active. Display-only choices (chart layout, markers, reference
view, dBm/counts, console filters) never touch the device and stay usable.

A mode entered from the console is adopted by the tool of that mode (e.g. the generator tab can
switch off a generator started by hand) and locks out the others until `mode_exit`.

## Code layout

```
composeApp/src/
  commonMain/kotlin/io/github/ml3rc/sna/client/
    protocol/   wire data classes and limits (mirror PROTOCOL.md), SweepGrid (the firmware's grid)
    client/     SnaClient (requests, streams, events, mode holding), TrafficLog (console)
    session/    plain StateFlow state, no Compose:
                AppState (ports, connect, handshake) -> Session (one per connection) owning
                DeviceMonitor, ToolActivity/blockReason and one state class per tool
                (SweepState, RawSweepState, GeneratorState, PowerMeterState, ProbeState,
                CalibrationWizardState, SdiState, ManualState, HousekeepingState, ConsoleState)
    ui/         Compose screens (one per tab), Controls (shared widgets), SnaChart, Format
    data/       Touchstone/JSON export, FileStore (expect/actual file dialogs)
    transport/  SerialTransport (the platform seam), SnaUsbIdentity
  desktopMain/  entry point, jSerialComm transport, native file dialogs
  androidMain/  Activity, usb-serial-for-android transport, manifest
```

Everything that belongs to a connection lives in `Session`, so running tests, settings and
results survive tab changes; screens only read and drive it. New screens get their own state
class in `session/` rather than growing a shared one. Never create one chart-model producer per
changing key, and keep missing points `null` (gaps) all the way into `SnaChart`.

Long sessions: streamed sweep points are collected into their grid slots and published at most
about every 16 ms, 60 Hz (`LivePoints`; the console refreshes at the same rate), so long loop sweeps neither copy the list per point nor recompose the
chart per point; the console log is bounded.

## Prerequisites

- JDK 17–21 (the project targets Java 17; Temurin 21 is used here). Android Studio's bundled
  Java 25 and newer JDKs break the pinned build: pick a compatible Gradle JDK or set `JAVA_HOME`.
- Android target: Android SDK (compileSdk 35, minSdk 26) via Android Studio or `cmdline-tools`,
  with `ANDROID_HOME`/`local.properties` pointing at it.
- No system Gradle: `gradlew` / `gradlew.bat` bootstrap the pinned version.

## Build, run, test

```
gradlew.bat :composeApp:run                # desktop app
gradlew.bat :composeApp:desktopTest        # unit + desktop UI tests, no hardware needed
gradlew.bat :composeApp:packageMsi         # Windows installer (packageDmg / packageDeb on macOS / Linux)
gradlew.bat :composeApp:assembleDebug      # Android APK -> composeApp/build/outputs/apk/debug/
gradlew.bat :composeApp:installDebug       # ... onto a connected phone with USB debugging
gradlew.bat :composeApp:connectedDebugAndroidTest   # Android UI tests (emulator or device)
```

Installers are unsigned (expect an "unknown publisher" warning). Bump `packageVersion` for every
installer meant to be installed: Windows Installer can treat the same version as a no-op.

The tests use simulated serial transports. The desktop UI test saves rendered previews (from
simulated data, not measurements) under `composeApp/build/ui-previews/`. They do not replace
testing with the real device.

## Platform notes

- USB identity: both clients list only the firmware's **CAFE:4001** VID:PID (TinyUSB development
  IDs). Keep `firmware/src/usb_descriptors.c`, `transport/SnaUsbIdentity.kt` and Android's
  `res/xml/device_filter.xml` (decimal IDs) in sync.
- Desktop serial uses `TIMEOUT_READ_SEMI_BLOCKING` and asserts DTR; the firmware only talks to a
  host that asserts DTR. Android asserts DTR too and splits lines on raw bytes.
- Android keeps the USB session in an activity ViewModel across rotation and window resizing.
- Android's `FileStore` writes to app-private storage instead of a Storage Access Framework
  picker; replace it before shipping to other users.
- Dropdowns use a plain `Box` + `OutlinedButton` + `DropdownMenu` (Material3's "Exposed"
  dropdown API has changed between Compose Multiplatform versions).
