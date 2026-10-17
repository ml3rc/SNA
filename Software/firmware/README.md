# SNA firmware

RP2350B firmware, Raspberry Pi Pico SDK (C, bare-metal + TinyUSB), implementing
[`../protocol/PROTOCOL.md`](../protocol/PROTOCOL.md) over USB CDC-ACM.
Current version: **0.4.1** (protocol 3). 0.4.1 adds the robustness fixes below to 0.4.0; the
protocol is unchanged. It passes the host tests but is **not yet tested on hardware**.

## Overview

- **Measurement:** scalar S11/S21/S12/S22 from 50 MHz to 6 GHz, 50 or 75 ohm,
  with the 8 calibration sets stored in EEPROM and applied on the device.
  S11/S21 use source port 1, S12/S22 source port 2; receiver readings from
  different source states are never mixed.
- **Modes:** the RF hardware belongs to one mode at a time (`sweep`,
  `generator`, `power_meter`, `manual`). The host enters it with
  `mode_enter`, uses it and leaves it with `mode_exit`; a mode can only be
  entered from idle. A USB disconnect cancels the mode back to idle. See
  `PROTOCOL.md` "Modes".
- **Dual core:** core 1 owns all RF hardware in every mode: it runs the
  application layer (`rf_app_*`) and everything below it, nothing else.
  Core 0 owns USB, JSON, EEPROM, calibration and the front panel. The only
  link is `src/rf_task.c`: core 0 sends requests, core 1 answers each with
  one final reply (a sweep streams its points first, with backpressure).
  Core 0 converts and calibrates point N while core 1 measures point N+1;
  calibration curves are prefetched into RAM, so core 0 needs no I2C during
  a sweep. Design: [`../docs/dual-core-design.md`](../docs/dual-core-design.md).
- **Robustness (0.4.1):** a watchdog (8 s) resets the device when core 0
  stops looping or core 1 sits in one step for 6 s (`main.c`,
  `service_watchdog()`); cJSON nesting is limited to 8 levels, so a line of
  `[[[[...` is `bad_request` instead of recursing through core 0's stack into
  core 1's; a write that gives up partway is followed by a newline, so the
  host loses one junk line, not the next reply; a host that stops reading
  costs one 2 s stall, after which output is dropped until it reads again;
  unread input and unsent output of a USB session are discarded on
  disconnect; the reply loop handles one queue's worth per pass, so `abort`
  is read during fast sweeps; EEPROM I2C transfers have timeouts and the bus
  is clocked free at boot; a failed calibration-table read is retried on the
  next prefetch, and the EEPROM header is read twice before an unrecognized
  one is reformatted. Review: `docs/firmware-review.md`, 2026-10-17.
- **Settings:** `set` changes impedance and attenuation in any mode, also
  between sweep points; every sweep and generator start brings its own `z`
  and `atten`.
- **RF source:** the MAX2871, amplifier and attenuator are controlled together
  (`rf_state_set_source()`: OFF / PARKED / ON). The source boots PARKED (tuned to
  1 GHz, attenuator 127, port switch terminated) and stays running between
  sweeps so its temperature (and VCO/PLL behaviour) stays stable; only
  `manual src_en:false` turns it off. Parking always sets the attenuator to
  127 before terminating the port switch; switching it on does the reverse. The
  50/75 ohm switch may be changed while the source is on.
- **Filters and detector:** sweeps and the generator pick the lowest LPF whose
  cutoff x 0.8 still passes the frequency (947 MHz filter up to 757.6 MHz,
  1747 MHz up to 1397.6 MHz, 3500 MHz up to 2800 MHz, none above). The
  detector is on from boot; each coupler-path reading waits 100 us after the
  switch and takes the median of 16 ADC samples, plus 200 us after PLL lock
  on a new frequency. The PLL
  runs in CP20 charge-pump linearity mode (taken from LibreVNA: other modes
  give false unlock indications on LD), 100 MHz PFD, charge-pump code 15.
- **Lock handling:** retunes allow up to 20 ms for lock and require LD to stay
  high for 500 us; sweep start and generator start allow up to two seconds.
  Both waits give up when the request is cancelled (`abort`, `mode_exit`,
  `bootsel`, host gone). By default an unlocked point is still measured and
  flagged `locked:false`; `allow_unlocked:false` makes the sweep fail with
  the stage, point and frequency instead.
- **Other functions:** CW generator, single and continuous power meter
  (`power` events), detector probe, MAX2871 temperature (`src_temp_read`,
  cached during a sweep or with the source on) and a front-panel SDI test
  that runs the stored threshold points without a PC.
- **Boot self-test:** core 1 checks PLL lock at 1 GHz and a parked detector
  reading inside 2200..3000 counts (window based on the single parked
  measurement in `docs/generator-and-power-meter.md`, not characterized);
  core 0 checks EEPROM and USB init. Failures appear in
  `status.init_errors` and as a blink code.
- **LEDs:** port/impedance LEDs follow their switches; BUSY/READY show the
  load of both cores (linear PWM brightness, 1 ms windows: idle = READY only, 50 % = both at 50 %,
  full load = BUSY only); BOOTED blinks fast during init, then once per
  second while the firmware is alive. After a failed boot check BOOTED stays
  off and the red SDI LED repeats N short blinks per failed check
  (1 EEPROM, 2 source lock, 3 detector, 4 USB).

**Accuracy limits:** the detector slope is one fixed value across the band;
PLL/switch settling budgets and the LPF selection boundaries still need bench
characterization. Scalar calibration cannot correct errors requiring phase.
SDI thresholds support 50 MHz through UINT32_MAX Hz (their storage format).

## Building

The firmware is built with [devenv](https://devenv.sh) (Nix) in WSL or on
Linux. `devenv.nix` / `devenv.yaml` describe the whole toolchain: CMake,
Ninja, `gcc-arm-embedded`, host gcc (the Pico SDK builds picotool with it),
Python 3 and the Pico SDK **2.2.0** with its submodules. Nix fetches all of
it, and `devenv.lock` pins the exact versions, so every machine builds with
the same toolchain. Nothing has to be installed by hand besides Nix and devenv.

### One-time setup (WSL)

1. Install Ubuntu for WSL (from Windows: `wsl --install -d Ubuntu`) and open it.
   systemd must be running (`ps -p 1 -o comm=` prints `systemd`; it is the
   default on current Ubuntu images).
2. Install Nix (multi-user; asks for your sudo password):
   ```sh
   sh <(curl --proto '=https' --tlsv1.2 -L https://nixos.org/nix/install) --daemon
   ```
3. Close and reopen the Ubuntu terminal, then install devenv:
   ```sh
   nix-env --install --attr devenv -f https://github.com/NixOS/nixpkgs/tarball/nixpkgs-unstable
   ```
4. Optional, lets devenv use its binary cache (faster first setup):
   ```sh
   echo "trusted-users = root $USER" | sudo tee -a /etc/nix/nix.conf && sudo systemctl restart nix-daemon
   ```

### Build

From this `firmware/` directory (in WSL, e.g.
`cd /mnt/c/Users/<you>/repos/private/SNA/Software/firmware`):

```sh
devenv shell fw-build   # configure if needed, build -> build/sna_firmware.uf2
devenv shell            # or enter the environment and run:
fw-build                #   build
fw-clean                #   delete build/
```

Output: `build/sna_firmware.uf2` (plus `.elf`/`.bin`/`.map`, see
`pico_add_extra_outputs` in `CMakeLists.txt`). The first run downloads the
toolchain and SDK and builds picotool, which takes a while; later builds are
incremental. `fw-build` reconfigures from scratch when the cached
`PICO_SDK_PATH` no longer matches the SDK of the environment (CMake caches it).

Notes:

- Builds on `/mnt/c/...` work but are slow; a clone inside the WSL file system is much faster.
- Commit `devenv.lock` whenever it changes (`devenv update` refreshes the pinned versions).
- Flash from Windows (see below); it reads `build/` directly. From WSL the USB
  device would have to be passed through with `usbipd-win`.

## Tests

Host regression tests, run on Windows with a host GCC (no device access):

```powershell
.\tests\run-host-tests.ps1
```

They use Win32 threads and critical sections (`rf_task_test`,
`tests/stubs/queue_stub.c`), so they don't run in WSL. What each test covers:
[`tests/README.md`](tests/README.md).

## Flashing

### Via SWD (J202)

Works without a USB host connection (e.g. the board powered from a bench
supply). The stock `scoop install openocd` has no RP2350 support; use the
Raspberry Pi build from `raspberrypi/pico-sdk-tools`, kept locally in
`.toolchain/openocd-rp2350/` (gitignored):

```powershell
.\.toolchain\openocd-rp2350\openocd.exe -s .toolchain\openocd-rp2350\scripts `
  -f interface/cmsis-dap.cfg -f target/rp2350.cfg `
  -c "adapter speed 4000" -c "program build/sna_firmware.elf verify reset exit"
```

In Git Bash/WSL use `\` line continuations and forward slashes.
`adapter speed 4000` is optional but much faster than OpenOCD's default
(~100 kHz); lower it to 1000–2000 if verification fails.

probe-rs does not work with the current debug probe: its firmware (v1.0.1)
is older than probe-rs 0.32.0 requires (2.2.0). After a probe firmware
update it would be:

```powershell
probe-rs download build/sna_firmware.elf --chip RP2350B --protocol swd
probe-rs reset --chip RP2350B
```

### Via BOOTSEL/UF2

Hold BOOTSEL while plugging in USB (or send `{"cmd":"bootsel"}` to running
firmware, see `PROTOCOL.md`); the board enumerates as a USB drive. Copy
`build/sna_firmware.uf2` onto it and it reboots into the new firmware.
`picotool load build/sna_firmware.uf2 -f` does the same from the command line.

## Debugging

SWD via J202: run OpenOCD as above without the `-c "program ..."` argument
as a GDB server, then `arm-none-eabi-gdb build/sna_firmware.elf` and
`target remote localhost:3333`. With a probe new enough for probe-rs,
`probe-rs attach --chip RP2350B build/sna_firmware.elf` also works.

## Source layout

Each layer calls only the one directly below it, and every layer has its own
prefix (functions lower case, macros/enum values/types upper case):

| Layer | Prefix | Where | Knows |
|---|---|---|---|
| Application | `rf_app_` / `RF_APP_` | `src/rf_control/application.c` | which application owns the RF hardware (IDLE / SWEEP / GENERATOR / PWR_DETECTOR / MANUAL), raw measurements, boot self-test, pure count/dB conversions |
| State | `rf_state_` / `RF_STATE_` | `src/rf_control/state.c` | the current hardware configuration and the safe order of every change (park/route, power-up double write, LPF choice) |
| Block (rf_control) | `rf_ctrl_` / `RF_CTRL_` | `src/rf_control/{source,coupler,detector,ports}.c` | the board wiring: switch truth tables, which chips form a block |
| HAL | `hal_` / `HAL_` | `src/hal/` | one chip each: registers and GPIO protocol, nothing about the board |

The application layer runs on core 1 only and depends on nothing above it
(cancellation comes in through `rf_app_hooks_t`); `application.h` exposes
none of the layers below. Core 0 code (`main.c`, `protocol.c`,
`sdi_test.c`, `measurement.c`) reaches the RF hardware only through
`rf_task_*` requests and uses `rf_app_*` directly only for types and the
pure conversions. `eeprom.c` is the storage counterpart of `rf_control/` on
top of `hal/eeprom_24aa32a.c` and is used on core 0 only.

How a request travels through these layers and the two cores:
[`../docs/firmware-dataflow.md`](../docs/firmware-dataflow.md).

| File | Purpose |
|---|---|
| `boards/sna.h` | Custom board header — pin map (real GPIO numbers, verified against the datasheet), flash size, crystal config |
| `src/main.c` | Core 0: boot sequence (board GPIO, boot blink timer, core 1 start, EEPROM/USB init, init error code), main loop, USB CDC line reader, SDI button debounce, front-panel LED/button GPIOs (the only GPIOs not owned by a `hal/` module) |
| `src/protocol.c` | Core 0: JSON parse and validation per `PROTOCOL.md`, requests to core 1 (pending table of 8), replies and sweep points to JSON, continuous power meter events, abort/stall/host-gone cancellation, EEPROM commands |
| `src/rf_task.c` | The only link between the cores: request and reply queues (8 each), the core-1 loop (requests, sweep points, backpressure, cancellation by sequence number) |
| `src/measurement.c` | Core 0: raw detector codes to S-parameters in dB, minus the EEPROM calibration from the RAM cache |
| `src/sdi_test.c` | Core 0: non-blocking front-panel SDI test (mode enter, sweep, mode exit, `sdi_test_result` event, result LED pulses) |
| `src/rf_control/` | Board-level RF control on core 1, three layers (see `rf_control.h`, the only header other code includes). Application: `application.c` (mode state machine, sweep sessions and points, generator, power meter, manual control, probe, boot self-test, pure conversions). State: `state.c` (source OFF/PARKED/ON, port, impedance, attenuation, frequency, LPF, coupler path, detector). Blocks, the only HAL callers: `source.c` (PLL, LPF bank, attenuator, amp), `coupler.c`, `detector.c`, `ports.c` (port/impedance switches and their LEDs) |
| `src/eeprom.c` | Core 0: structured calibration/config storage (header, cal grid, SDI thresholds, 8 cal tables, RAM cache and interpolation; format 3) per `../protocol/EEPROM_LAYOUT.md`, built on `hal/eeprom_24aa32a.c` |
| `src/hal/max2871.c` | MAX2871 synthesizer driver (bit-banged 4-wire, enable/lock-detect, frequency-set register programming) |
| `src/hal/ad8318.c` | AD8318 log detector driver (enable + ADC read) |
| `src/hal/trf37a73.c` | TRF37A73 amp driver (PWDN control) |
| `src/hal/hmc1119.c` | HMC1119 digital attenuator driver (7-bit parallel + latch) |
| `src/hal/rf_switch.c` | Generic 2-line RF switch driver, shared by the LPF/coupler/port/impedance switch banks |
| `src/hal/eeprom_24aa32a.c` | Raw 24AA32A I2C byte read/write (page-write-aware), no knowledge of what the bytes mean |
| `src/third_party/cJSON/` | Vendored cJSON v1.7.19 (unmodified upstream, own CMake target so it's exempt from our `-Wall -Wextra`), see its `README.md` |
| `tests/` | Host regression tests and the SDK/TinyUSB stubs they compile against, see `tests/README.md` |
| `src/usb_descriptors.c`, `src/tusb_config.h` | TinyUSB CDC-only device descriptors |
