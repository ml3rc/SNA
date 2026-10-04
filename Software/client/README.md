# SNA client

Desktop **1.2.2** / Android **0.2.2** refines the instrument workspace for a class
presentation: charcoal surfaces, restrained blue interaction accents, simple
panel borders, sans-serif UI text and monospace readouts. Startup puts USB
connection first and briefly explains reflection/transmission measurements.
Desktop uses a navigation rail when space permits; smaller windows and phones
use tabs. Trace colors identify the plotted signals, secondary actions have
quieter buttons, and larger plot labels improve readability. Exports and plot
view controls are grouped with the response display, leaving acquisition
controls focused on starting/stopping sweeps. Calibration and self-test controls
wrap for phone widths, with scrolling for settings and diagnostics.

Both clients only list and open USB devices with the
firmware's existing **CAFE:4001** vendor/product ID. Android's USB attachment
filter uses the same pair, so unrelated USB devices no longer offer to launch
the app. No firmware update is required. These are development IDs shared with
TinyUSB examples, rather than an allocated SNA-exclusive identity; keep the
firmware descriptor, `SnaUsbIdentity.kt` and `device_filter.xml` in sync when
allocated IDs are assigned. Connection still requires a successful protocol `ping`.

The client defaults to **50 MHz–6 GHz** and validates
sweep and calibration ranges before sending them. Calibration grids from older
firmware are preserved; the wizard explains when an old grid must be cleared
explicitly before recapture. SDI uses 50 MHz through UINT32_MAX Hz.

Kotlin Multiplatform + Compose Multiplatform app targeting desktop (Windows/Linux/macOS via the
JVM) and Android. No iOS target (out of scope for the SNA project). Speaks the protocol in
[`../protocol/PROTOCOL.md`](../protocol/PROTOCOL.md) over USB CDC-ACM.

## Prerequisites

- JDK 17 or 21 recommended (project targets Java 17; installed JDK 20 was also tested).
  Android Studio's bundled Java 25 is incompatible with this pinned build; choose a
  compatible Gradle JDK in Android Studio or set `JAVA_HOME` in the terminal.
- For the Android target: Android SDK (compileSdk 35, minSdk 26) — either via Android Studio or
  the standalone `cmdline-tools` + `sdkmanager`, with `ANDROID_HOME`/`local.properties` pointing
  at it
- No system-wide Gradle needed — `./gradlew` / `gradlew.bat` bootstrap their own (pinned in
  `gradle/wrapper/gradle-wrapper.properties`); first run downloads it

## Build & run — desktop

```
./gradlew :composeApp:run          # Linux/macOS
gradlew.bat :composeApp:run        # Windows
```

Packaged installers (native per-OS, built on that OS — no cross-packaging):

```
./gradlew :composeApp:packageDmg   # macOS
./gradlew :composeApp:packageMsi   # Windows
./gradlew :composeApp:packageDeb   # Linux
```

Unsigned for now (see project decisions) — expect an "unknown publisher" warning on first run.

## Build & run — Android

```
./gradlew :composeApp:assembleDebug
```

On this Windows PC, the verified command is:

```powershell
$env:JAVA_HOME = 'C:\Program Files\Java\jdk-20'
.\gradlew.bat :composeApp:assembleDebug
```

Builds a debug-key-signed APK at `composeApp/build/outputs/apk/debug/composeApp-debug.apk`.
To install on a connected Android device with USB debugging enabled, run
`gradlew.bat :composeApp:installDebug`. Building alone does not install the app.
Needs `android.hardware.usb.host` (declared in the manifest) —
most phones support USB host mode but check yours if deploying to a specific device.

The Android USB transport asserts DTR for the SNA's TinyUSB CDC connection, uses
the serial library's `start()` API, and registers its permission receiver with
the explicit export flag required by current Android versions. USB operation
still requires testing on an actual phone with the SNA attached.

Android 0.1.1 retains the USB session in an activity ViewModel across rotation and
window resizing. Sweep inputs and the selected tab use saved UI state. Phone layouts
use smaller typography, wrapping controls, a scrollable sweep page, and vertically
stacked separate charts, while keeping system font scaling and touch targets.

## Structure

```
composeApp/src/
  commonMain/     shared: protocol models, SnaClient, UI (Compose), Touchstone/JSON export
  desktopMain/    entry point, jSerialComm-backed SerialTransport, native file dialogs
  androidMain/    Activity, usb-serial-for-android-backed SerialTransport, manifest
```

`SerialTransport` (commonMain) is the platform-abstraction seam — `SnaClient` and the UI never
touch jSerialComm or usb-serial-for-android directly.

**UI, once connected**: a 4-tab session (`ui/App.kt`) — Sweep (all 4 S-parameters, Canvas chart
with hover values, merged/separate views, single/loop sweeps, relative output attenuation,
Touchstone/JSON export), Calibration (SLOT wizard: Open/Short/Thru capture per impedance mode,
shared-grid detection/locking, compute+upload+verify — `ui/CalibrationScreen.kt` +
`ui/CalibrationWizardState.kt`), SDI (editable front-panel self-test threshold list, with
S-parameter/comparison per point — `ui/SdiConfigScreen.kt`), and HK (device status, MAX2871
temperature, detector ADC, manual refresh — `ui/HkScreen.kt`). Each new screen owns its own small
plain state class (same style as `ui/SnaViewModel.kt` — StateFlow fields, no androidx ViewModel)
rather than growing `SnaViewModel` into a God object.

Desktop regression tests: `gradlew.bat :composeApp:desktopTest`. The reconnect test uses a
simulated serial transport and needs no connected device. The desktop UI test
covers discovery, connection, trace selection, workspace navigation and disconnect.
It saves actual rendered previews under `composeApp/build/ui-previews/`, including
a response curve generated by the test's simulated transport (not measured RF data).

Android regression tests: `gradlew.bat :composeApp:connectedDebugAndroidTest`
with an emulator or test device available. These check session identity across
activity recreation and portrait/landscape rotation, plus sweep input restoration
and scrolling at phone width using a simulated transport. They do not replace
testing real USB hardware on the phone.

## Known scaffold simplifications (see inline `// TODO` / doc comments)

- `FileStore`'s Android implementation writes to app-private storage rather than prompting a
  proper Storage Access Framework picker — fine for bring-up, replace before shipping.
- Material3's "Exposed" dropdown family (`ExposedDropdownMenuBox`/`ExposedDropdownMenu`) turned
  out to have the same kind of cross-version API volatility Vico's chart API has — `ui/
  SdiConfigScreen.kt` uses a plain `Box` + `OutlinedButton` + `DropdownMenu` combo instead (see
  `SimpleDropdown` in that file), which has been stable for a long time. Prefer that pattern over
  the "Exposed" family for any future dropdown in this app.
