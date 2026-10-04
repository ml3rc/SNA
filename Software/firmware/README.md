# SNA firmware

Firmware **0.1.10** forces a 250 ms USB detach at startup so Windows detects
a fresh device after SWD reset/flashing. It also supports optional sweep
`raw_adc:true` for per-source detector diagnostics. Calibration storage and
normal sweep dB values retain their existing behavior.

Firmware **0.1.9** supports **50 MHz–6 GHz** for sweeps, manual source control,
demo sweeps and new calibration grids. `ping` advertises both limits. Existing
format-2 calibration is preserved and can still be used within the supported
band. SDI supports 50 MHz through UINT32_MAX Hz with its current storage format.

The normal build must use `SNA_PLL_CP20_EXPERIMENT=ON` for this board. CP20 is now
the default, and the variant-build script leaves `build3/sna_firmware.elf` on CP20.
Existing CMake caches retain their previous setting: configure them explicitly
with `cmake -S . -B build3 -DSNA_PLL_CP20_EXPERIMENT=ON` before rebuilding.
The CP20-off artifact is retained only for controlled comparison, not normal flashing.

RP2350B firmware, Raspberry Pi Pico SDK (C, bare-metal + TinyUSB), implementing
[`../protocol/PROTOCOL.md`](../protocol/PROTOCOL.md) over USB CDC-ACM.

## Status

Firmware version **0.1.6** implements USB CDC commands, MAX2871 tuning,
independent source-port/coupler/impedance/filter control, scalar measurement,
calibration storage and front-panel SDI tests/demo sweeps.

USB sweeps and SDI acquisition run on core 1. Core 0 owns USB, EEPROM,
conversion and calibration, processing each queued point while core 1 acquires
the next. Calibration curves are cached in RAM. Idle manual/demo controls keep
exclusive RF access on core 0 after acknowledged worker shutdown. See
[`../docs/dual-core-design.md`](../docs/dual-core-design.md) for ownership,
validation and prepared baseline/CP20 artifacts. Version 0.1.4 was profiled;
0.1.5 is built but not flashed. USB sweeps now return fresh provisional values
after LD timeout with `locked:false`; `allow_unlocked:false` retains strict
behavior. Client calibration captures and SDI remain strict. See
[`../docs/pll-lock-research.md`](../docs/pll-lock-research.md) for findings,
fallback semantics and the 0.1.5 images.

Version 0.1.6 implements the user-confirmed MAX2871 design: 100 MHz PFD and
charge-pump code 15 (about 5.11 mA at RSET=5.1 kohm). The external filter is
designed for 450 kHz bandwidth; there is no firmware bandwidth register.
Client 1.1.5 calibration capture/verification uses `atten:0` (+5 dBm RFOUTA,
0 dB attenuator setting) and firmware restores the preceding attenuation after
RF shutdown, including failure and cancellation. Artifacts are under
`artifacts/0.1.6/`. The authorized CP20 image flash verified; seven strict hardware
sweeps passed (2207 points), including 1001 points in 2.031 s at maximum level.
No calibration tables were modified. See
[`../docs/sweep-profiles/cp20-validation-20261003.md`](../docs/sweep-profiles/cp20-validation-20261003.md).

The source port stays fixed across a reference/response pair: S11/S21 use
source port 1; S12/S22 use source port 2. Receiver readings from different
source states are captured separately. Port/impedance LEDs update when their
controls change; BUSY/READY cover USB and SDI measurements.

The GPIO map and port, coupler and LPF switch tables retain the board mappings
confirmed during bring-up. See `boards/sna.h` and `../protocol/PINMAP.md`.
The MAX2871 register programming retains the previously verified board constants
(100 MHz reference, RSET 5.1 kohm); this review did not retune the PLL.

**2026-10-03 audit:** corrected source/receiver conflation, per-source reading
reuse, blocked Abort handling, invalid-input conversions, lock-failure reporting,
USB stall/disconnect shutdown, EEPROM write-error reporting and oversized CDC
line handling. Built for RP2350B and tested against simulated hardware; see
[`../docs/firmware-review.md`](../docs/firmware-review.md) for evidence and limits.
The 0.1.3 and 0.1.4 images were flashed and measured; 0.1.5 awaits confirmation.

**Bring-up gate:** `SNA_BRINGUP_NO_RF_SOURCE` is ON by default and keeps
MAX2871 CE/RFOUT_EN low. The existing `build3` cache is OFF, matching the
previous real-device bring-up build; rebuilding does not change that setting.
Retunes allow up to 20 ms for lock and require LD to remain high for 100 us.
This is a bounded fallback, not a fixed delay or a measured board specification.
Startup still allows up to two seconds. Lock errors include startup/retune stage,
point index and requested frequency.

Optional full-sweep profiling extends the retune deadline (2 seconds by default)
and records frequency programming, routing/settling, lock, ADC and processing
times. Timed-out points are marked invalid and profiling continues. See
[`../docs/sweep-profiling.md`](../docs/sweep-profiling.md) for capture commands
and the definitions of each timing field.

With the gate ON, the source cannot emit RF; strict acquisition reports failure,
and provisional acquisition still carries `locked:false`. No build command flashes
the device.

**Accuracy limits:** detector slope is approximated with one fixed value across
the band; PLL/switch settling budgets and the LPF selection boundaries still
need bench characterization. Scalar calibration cannot correct errors requiring
phase. Existing transmission calibration must be recaptured after installing
the corrected source-routing implementation. EEPROM format remains 2; the audit
introduces no automatic calibration wipe. SDI thresholds currently support only
50 MHz through UINT32_MAX Hz; higher frequencies are explicitly rejected.

Host regression tests (GCC and PowerShell, no device access):

```powershell
.\tests\run-host-tests.ps1
```

## Building

### Option A: Raspberry Pi Pico VS Code extension (recommended)

Install the official **Raspberry Pi Pico** extension in VS Code, then open
this `firmware/` folder. The extension handles installing the ARM toolchain,
the Pico SDK (pick an SDK version with RP2350 support, 2.0.0 or newer),
CMake, Ninja, and OpenOCD/picotool for you, on Windows, Linux, and macOS —
this is by far the least friction path, especially for getting a matching
toolchain/SDK version pinned down.

When prompted for a board, there is no "SNA" entry in the extension's board
list (it's a custom board) — configure the project manually instead: set
`PICO_BOARD=sna` and point `PICO_SDK_PATH` at the SDK it installed, then let
it drive CMake/Ninja as usual. `.vscode/settings.json.example` shows the
CMake environment variable shape; copy it to `.vscode/settings.json` (which
is gitignored, since the real path is machine-specific) and fill in the
real `PICO_SDK_PATH`.

### Option B: manual CLI build

Requires: `arm-none-eabi-gcc` toolchain, CMake ≥ 3.13, Ninja (or Make),
Python 3 (the SDK's build-time scripts need it), and a checkout of
[pico-sdk](https://github.com/raspberrypi/pico-sdk) at a version with RP2350
support (2.0.0+), with its submodules initialized (`git submodule update
--init`).

Known-working combination (2026-09-30): pico-sdk **2.2.0**, ARM GNU
Toolchain **15.2.1** (`gcc-arm-none-eabi` via scoop, or the official ARM
installer), Ninja, CMake 3.30. `scoop` commands below work the same in any
shell; the rest is shown for both PowerShell (this project's actual
day-to-day shell on Windows -- not WSL/Git Bash, despite earlier revisions
of this doc assuming that) and POSIX sh (Git Bash/WSL/Linux/macOS).

```powershell
scoop bucket add extras
scoop install ninja make gcc-arm-none-eabi
git clone --depth 1 --branch 2.2.0 https://github.com/raspberrypi/pico-sdk.git .toolchain/pico-sdk
cd .toolchain/pico-sdk; git submodule update --init --depth 1; cd ../..
```

Then, from this `firmware/` directory:

```powershell
$env:PATH = "$(Resolve-Path ~\scoop\apps\gcc-arm-none-eabi\current\bin);$env:PATH"
cmake -S . -B build3 -G Ninja -DPICO_SDK_PATH="$(Resolve-Path .toolchain/pico-sdk)"
cmake --build build3
```

<details>
<summary>POSIX sh (Git Bash/WSL/Linux/macOS) equivalent</summary>

```sh
scoop bucket add extras
scoop install ninja make gcc-arm-none-eabi
git clone --depth 1 --branch 2.2.0 https://github.com/raspberrypi/pico-sdk.git .toolchain/pico-sdk
cd .toolchain/pico-sdk && git submodule update --init --depth 1 && cd ../..

export PATH="$HOME/scoop/apps/gcc-arm-none-eabi/current/bin:$PATH"
cmake -S . -B build3 -G Ninja -DPICO_SDK_PATH="$(pwd)/.toolchain/pico-sdk"
cmake --build build3
```
</details>

`PICO_SDK_PATH` is a **cached** CMake variable -- once `build3`'s cache has
it, a plain environment-variable change on a later `cmake --build` is
silently ignored (CMake only reads the env var when the cache entry doesn't
already exist). This matters concretely on this project: if you also have
the Raspberry Pi Pico **VS Code extension** installed and ever open this
folder in VS Code, it may auto-reconfigure `build3` against its own
globally-managed SDK copy (`~/.pico-sdk/sdk/<version>`), which has a
different internal layout than the 2.2.0 tree pinned here and fails with
`add_subdirectory`/`memmap_default.cmake` errors. If that happens, delete
`build3` and reconfigure with the explicit `-DPICO_SDK_PATH=...` command
above (the `-D` forces the cache, unlike a bare env var) -- no need to
touch anything VS Code-related, just redo the one command.

Output: `build3/sna_firmware.uf2` (plus `.elf`/`.bin`/`.map` — see
`pico_add_extra_outputs` in `CMakeLists.txt`). First configure also builds
`picotool` from source (pico-sdk 2.x no longer bundles a prebuilt one) — this
adds a couple of minutes to the first `cmake --build` only.

## Flashing

### Via SWD (J202) — the only option when USB isn't connected to a host

If the board's USB-C is powered from something other than a PC (bench
supply, etc.) and only the SWD debug header is actually connected to the
development machine, flash over SWD instead of BOOTSEL/UF2.

**OpenOCD** is the one that actually works in this environment — the stock
`scoop install openocd` build predates RP2350 support, so a separate build
from `raspberrypi/pico-sdk-tools` is vendored at
`.toolchain/openocd-rp2350/` (gitignored, not part of the repo):

```powershell
.\.toolchain\openocd-rp2350\openocd.exe -s .toolchain\openocd-rp2350\scripts `
  -f interface/cmsis-dap.cfg -f target/rp2350.cfg `
  -c "adapter speed 4000" -c "program build3/sna_firmware.elf verify reset exit"
```

(the backtick line-continuations are PowerShell syntax — in Git Bash/WSL,
use `\` instead and forward slashes throughout. `adapter speed 4000` isn't
required but speeds up flashing a lot over the ~100kHz OpenOCD otherwise
defaults to; back off to 1000–2000 if it ever causes verify failures.)

**probe-rs** would be simpler/more modern, but doesn't currently work with
the debug probe connected to this board — its firmware (v1.0.1) predates
probe-rs 0.32.0's minimum (2.2.0). If the probe's own firmware ever gets
updated (see the Status section above), the equivalent would be:
```powershell
probe-rs download build3/sna_firmware.elf --chip RP2350B --protocol swd
probe-rs reset --chip RP2350B
```

### Via BOOTSEL/UF2 — once USB is connected to a PC

Hold BOOTSEL while plugging in USB (or, once firmware is already running,
send the protocol's `{"cmd":"bootsel"}` command — see PROTOCOL.md) to make
the board enumerate as a USB mass-storage drive, then copy
`build3/sna_firmware.uf2` onto it. It reboots into the new firmware
automatically. `picotool load build3/sna_firmware.uf2 -f` does the same
without manual drag-and-drop, once picotool is built (see above) or on PATH.

## Debugging

SWD via the J202 header. With a probe whose firmware is new enough for
probe-rs (see above — not the case for the probe currently in use here),
`probe-rs attach --chip RP2350B build3/sna_firmware.elf` gives an
interactive RTT/register session. Otherwise, run OpenOCD (same invocation
as the Flashing section, swapping the `-c "program ..."` for nothing) as a
GDB server and point `arm-none-eabi-gdb build3/sna_firmware.elf` at
`target remote localhost:3333`. The RP2350B's dual Cortex-M33 cores debug
the same way as any other RP2350-based board once `PICO_PLATFORM=rp2350-arm-s`
is set correctly (done in `CMakeLists.txt`).

## Source layout

Two layers below `protocol.c`: `src/hal/` is one driver module per chip
(knows a chip's registers/GPIO protocol, nothing about how it's wired on
this board); `rf_control.c`/`eeprom.c` compose those into the
board-level/application-facing API (knows the wiring — which switches gang
together for a port selection, the EEPROM's byte layout, safe startup
state — but not chip register details). `protocol.c` only ever calls into
the composition layer, never into `hal/` directly.

| File | Purpose |
|---|---|
| `boards/sna.h` | Custom board header — pin map (real GPIO numbers, verified against the datasheet), flash size, crystal config |
| `src/main.c` | Init sequencing, USB CDC line reader, SDI test button poll loop, front-panel LED/button GPIOs (the only GPIOs not owned by a `hal/` module) |
| `src/protocol.c` | Command dispatch implementing `PROTOCOL.md` |
| `src/rf_control.c` | Board-level RF composition — which switches/HAL calls implement `rf_set_port`/`rf_set_z`/`rf_select_coupler_path`/etc, plus `rf_measure_point()` (per-source acquisition and scalar dB conversion) |
| `src/eeprom.c` | Structured calibration/config storage (header, cal grid, SDI thresholds, 8 cal tables, interpolation) per `../protocol/EEPROM_LAYOUT.md`, built on `hal/eeprom_24aa32a.c` |
| `src/hal/max2871.c` | MAX2871 synthesizer driver (bit-banged 4-wire, enable/lock-detect, frequency-set register programming) |
| `src/hal/ad8318.c` | AD8318 log detector driver (enable + ADC read) |
| `src/hal/trf37a73.c` | TRF37A73 amp driver (PWDN control) |
| `src/hal/hmc1119.c` | HMC1119 digital attenuator driver (7-bit parallel + latch) |
| `src/hal/rf_switch.c` | Generic 2-line RF switch driver, shared by the LPF/coupler/port/impedance switch banks |
| `src/hal/eeprom_24aa32a.c` | Raw 24AA32A I2C byte read/write (page-write-aware), no knowledge of what the bytes mean |
| `src/third_party/cJSON/` | Vendored cJSON v1.7.19 (unmodified upstream, own CMake target so it's exempt from our `-Wall -Wextra`) — replaced the project's original hand-written JSON parser/writer once the toolchain was actually working and a real library could be fetched and verified by compiling, not reproduced from memory; see that folder's own `README.md` |
| `src/usb_descriptors.c`, `src/tusb_config.h` | TinyUSB CDC-only device descriptors |
