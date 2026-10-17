# SNA host protocol (protocol 3, firmware 0.4.x)

Single source of truth for the firmware ↔ client wire protocol. Both `firmware/` and `client/` implement against this document — update it first, then both sides.

## Transport

USB CDC-ACM (virtual serial port), 115200 baud nominal (irrelevant over USB CDC but set for compatibility). Full-Speed USB (12 Mbit/s), no driver install needed on Windows/Linux/macOS; Android via `usb-serial-for-android`.

## Framing

One JSON object per line, terminated by `\n`. PC → device is always a `{"cmd": ..., "id": N, ...}` request. Device → PC is either:
- a **response** to a request, echoing the same `"id"`, always containing `"ok": true|false` (`"error": "..."` when false), or
- a **stream** of intermediate results (sweep points), also echoing `"id"`, followed by a final response with `"done": true`, or
- an **unsolicited event**, using `"event"` instead of `"id"`/`"cmd"` (continuous power readings, the front-panel SDI test result).

`id` is chosen by the client (any JSON value), used purely for request/response correlation — the device does not interpret it. Every request gets exactly one reply (a sweep: its points, then its final reply). Correlate by `id`: a request the device can answer at once (an invalid one, `ping`, `abort`, the EEPROM commands) may be answered before earlier requests that still run on the RF side, and points of a running sweep interleave with the replies to commands sent during it. Requests that reach the RF side are answered in the order they were sent.

Frequencies are always integer Hz, written as plain integers (never exponent notation).

Lines longer than 8 KB and JSON nested deeper than 8 levels are answered with `bad_request`. Input the device has
not read yet when the host disconnects (drops DTR) is discarded, so it never runs in the next session. If the device
gives up on a line partway (the host stopped reading for 2 s), it starts the next line with a newline: the host sees
one unparseable line and should skip it. A device that hangs resets itself through its watchdog (about 8 s) and
re-enumerates.

## Errors

`{"id":N,"ok":false,"error":"<code>", ...}`:

| Code | Meaning |
|---|---|
| `bad_request` | The line is not a JSON object with a string `cmd` |
| `unknown_cmd` | Unknown `cmd` |
| `bad_args` | A field is missing or invalid; `"field":"<name>"` names it |
| `busy` | Not allowed while a sweep (or the front-panel SDI test) runs, or the device is saturated with requests |
| `mode_active` | `mode_enter` while not idle; carries `"mode"` |
| `wrong_mode` | Command of another mode; carries `"mode"` |
| `output_active` | `manual lpf`/`cplr`/`src_freq` while the source is on |
| `tune_failed` | The synthesizer rejected the frequency |
| `source_not_locked` | No stable PLL lock (generator start, strict sweep) |
| `aborted` | The sweep was ended by `abort`, `mode_exit` or a host that stopped reading |
| `not_calibrated` | `cal_get` of a set without data |
| `grid_mismatch` | `cal_set` with an `f0`/`f1` other than the stored grid's |
| `storage_error` | The EEPROM could not be written or read |
| `not_valid` | `src_temp_read` has no valid reading |

Arguments are validated before the mode is checked, so invalid arguments get `bad_args` in any mode.

## Modes

The RF hardware belongs to one mode at a time. The host enters a mode, uses it and leaves it again:

| Mode | Commands that need it |
|---|---|
| `idle` | — (the only mode another one can be entered from) |
| `sweep` | `sweep` (any number of them) |
| `generator` | `gen_start`, `gen_stop`, `probe_read` |
| `power_meter` | `power_read`, `power_start`, `power_stop` |
| `manual` | `manual`, `probe_read` |

```
→ {"cmd":"mode_enter","id":1,"mode":"generator"}
← {"id":1,"ok":true,"mode":"generator"}
→ {"cmd":"gen_start",...}
→ {"cmd":"gen_stop",...}
→ {"cmd":"mode_exit","id":5}
← {"id":5,"ok":true,"mode":"idle"}
```

- `mode_enter` works only from `idle` (`mode_active` otherwise). Unknown modes and `idle` are `bad_args`.
- `mode_exit` returns to `idle` from any mode, and is also `ok` when already idle. It cancels whatever runs: a
  running sweep ends (its final reply carries `"error":"aborted"`, then the `mode_exit` reply follows), a generator
  start still waiting for lock gives up, the generator and the continuous power meter stop. It terminates the output
  and leaves the source running and parked (powering it up again if `manual src_en:false` switched it off).
- `set`, the housekeeping commands (`ping`, `status`, `src_temp_read`) and the EEPROM commands (`cal_*`,
  `sdi_test_*`) need no mode.
- When the USB host disconnects, the device cancels the mode back to `idle`; `bootsel` does the same.
- The front-panel SDI test starts only from `idle`; it enters `sweep` itself and leaves it when done.

## Commands

### `ping`
```
→ {"cmd":"ping","id":1}
← {"id":1,"ok":true,"fw":"0.4.1","proto":3,"hw_rev":1,"freq_min_hz":50000000,"freq_max_hz":6000000000}
```
`hw_rev` is read from the EEPROM header. A client for protocol 3 refuses to drive any other `proto`.

### `status`
```
→ {"cmd":"status","id":2}
← {"id":2,"ok":true,"mode":"idle","sweeping":false,"z":50,"port":1,"atten":127,
   "gen_on":false,"pwr_on":false,"init_errors":[]}
```
- `mode`: the current mode (see Modes). During the front-panel SDI test it is `sweep`.
- `sweeping`: a sweep or the front-panel SDI test is running (other commands get `busy`).
- `port`: the selected source port (1 or 2), also while the output is terminated.
- `gen_f` (while `gen_on`), `pwr_port` (while `pwr_on`).
- `init_errors`: boot self-test failures, empty when the device booted fine: `eeprom` (EEPROM not readable or
  writable), `source_lock` (synthesizer did not lock at boot), `detector` (parked detector reading implausible).
  The front panel shows them as a blink code (see Front panel).

`status` is answered at all times, including during a sweep.

### `set` — settings (any time)
```
→ {"cmd":"set","id":3,"z":75,"atten":40}
← {"id":3,"ok":true,"z":75,"atten":40}
```
At least one of `z` (50|75) and `atten` (0..127, 0.25 dB steps relative to maximum output, not calibrated dBm;
measured ≈ −atten dB in dBm at 2 GHz, roughly ±10 dB over frequency, see `docs/generator-and-power-meter.md`).
Works in every mode and during a sweep, and never changes the mode: `z` is switched immediately, also under a live
output; `atten` changes a running output live and applies to a running sweep from its next point on. Whether a
change in the middle of a measurement makes sense is the host's call. The reply carries the resulting settings.

### `sweep` — frequency sweep, results streamed as measured (mode `sweep`)
```
→ {"cmd":"sweep","id":4,"f0":50000000,"f1":6000000000,"points":201,"log":false,"z":50,"atten":0,
   "params":["s11","s21"]}
← {"id":4,"pt":0,"f":50000000,"s11_db":-12.3,"s21_db":-0.5,"locked":true}
← {"id":4,"pt":1,"f":79750000,"s11_db":-11.9,"s21_db":-0.4,"locked":true}
  ...
← {"id":4,"ok":true,"done":true}
```
- `f0`, `f1`: integer Hz in [50000000, 6000000000], `f0 <= f1`. `points`: integer in [1, 4096]. `log`: optional,
  default `false`. Point `i` is at `f0 + (f1-f0)*i/(points-1)` in exact integer arithmetic (linear), or
  `round(exp(ln f0 + (ln f1 - ln f0)*i/(points-1)))` (log); the first and last points are exactly `f0` and `f1`.
  Clients that show placeholders before the points arrive compute the same grid.
- `z` (50|75) and `atten` (0..127) are **required**: every sweep brings its own impedance and source level, applied
  before it starts. They stay the current settings afterwards (`status`, `set`).
- `params` is **required**: a non-empty subset of `["s11","s21","s12","s22"]`, or of the coupler states below.
  S11/S21 use source port 1, S12/S22 source port 2. Requesting both directions adds a source change and a separate
  reference reading per point.
- `allow_unlocked`: optional, default `true`. After the bounded lock wait (2 s at startup, 20 ms per point) a point
  is measured anyway and flagged `"locked":false`; the sweep goes on and finishes `ok:true` with
  `"warning":"source_unlocked","unlocked_points":N`. With `false` the first lock failure ends the sweep:
  `{"id":4,"ok":false,"done":true,"error":"source_not_locked","stage":"startup|retune","pt":12,"f":5100000000}`.
- Every point carries `locked`. If calibration data for `z` covers the point, the S-parameter values are corrected
  (the device interpolates its stored grid onto the sweep's frequencies); otherwise they are raw and the point
  carries `"cal":false`.
- During a sweep only `status`, `ping`, `src_temp_read`, `set`, `abort` and `mode_exit` are accepted; everything
  else gets `busy`. `abort` ends the sweep (`"error":"aborted"`), the mode stays `sweep`. A host that disconnects or
  stops reading for 2 s ends it as well; the output is parked afterwards.

**Raw detector sweep:** `params` from `["p1_fwd","p1_bwd","p2_fwd","p2_bwd"]` (the coupler paths, numbered 0–3 as
in `manual cplr` and `probe_read`) instead of S-parameters, plus a **required** `port` (1|2): the source port for the
whole sweep. The measurement is the same; each requested path returns `<name>_raw` (median of 16 ADC codes, higher
code = less power) and `<name>_dbm` (detector-referred, uncalibrated, meaningful above about -60 dBm). Nothing is
divided by a reference and no calibration is applied, so points always carry `"cal":false`. S-parameters and paths
cannot be mixed, and `port` is only valid with paths (`bad_args`, field `params`/`port`).
```
→ {"cmd":"sweep","id":4,"f0":50000000,"f1":6000000000,"points":201,"z":50,"atten":64,"port":1,
   "params":["p1_fwd","p2_bwd"]}
← {"id":4,"pt":0,"f":50000000,"p1_fwd_raw":1329,"p1_fwd_dbm":-23.7,"p2_bwd_raw":1632,"p2_bwd_dbm":-33.7,
   "locked":true,"cal":false}
```

### `abort`
```
→ {"cmd":"abort","id":5}
← {"id":5,"ok":true}
```
Ends a running sweep (see `sweep`); `ok` also when nothing runs.

### `cal_set` / `cal_get` / `cal_clear` — calibration data, one of 8 sets
Set names: `s11_50`, `s21_50`, `s12_50`, `s22_50`, `s11_75`, `s21_75`, `s12_75`, `s22_75`.

All 8 sets share one frequency grid (see `EEPROM_LAYOUT.md`). The first `cal_set` after `cal_clear all` (or with no
set stored) establishes `f0`/`f1`; later sets must match (`grid_mismatch`).
```
→ {"cmd":"cal_set","id":6,"set":"s11_50","f0":50000000,"f1":6000000000,"points":[<float dB, 1..240 values>]}
← {"id":6,"ok":true}

→ {"cmd":"cal_get","id":7,"set":"s11_50"}
← {"id":7,"ok":true,"set":"s11_50","f0":50000000,"f1":6000000000,"points":[<240 values>]}
  (or ← {"id":7,"ok":false,"error":"not_calibrated"})

→ {"cmd":"cal_clear","id":8,"set":"all"}
← {"id":8,"ok":true}
```
`points` are finite dB values in [-327.68, 327.67] (stored as 0.01 dB `int16`), resampled onto 240 evenly spaced
points across `[f0, f1]`. `f0`/`f1` as for `sweep`. A failed write leaves that set invalid (`storage_error`).
The client computes the correction curve from raw Open/Short/Load/Thru sweeps and uploads the *result*; the device
only stores, interpolates and subtracts one curve per set.

### `sdi_test_set` / `sdi_test_get` — the standalone front-panel SDI test
```
→ {"cmd":"sdi_test_set","id":9,"points":[{"f":270000000,"thresh_db":-15,"s_param":"s11","cmp":"lt"},
                                         {"f":5940000000,"thresh_db":-12,"s_param":"s21","cmp":"gt"}]}
← {"id":9,"ok":true}

→ {"cmd":"sdi_test_get","id":10}
← {"id":10,"ok":true,"points":[...]}
```
0–15 points (an empty list disables the test). `f` as for `sweep`; `thresh_db` in [-327.68, 327.67];
`s_param` one of `s11`/`s21`/`s12`/`s22`; `cmp` `lt` (measured value must be less than the threshold) or `gt`.
See Front panel for the test itself.

### `manual` — single RF blocks, for bench diagnostics (mode `manual`)
```
→ {"cmd":"manual","id":11,"block":"lpf","value":2}
← {"id":11,"ok":true}
```
| `block` | `value` |
|---|---|
| `lpf` | 0–3: none, 3500 MHz, 1747 MHz, 947 MHz (sweeps and the generator select it automatically) |
| `cplr` | coupler path 0–3: port 1 fwd, port 1 bwd, port 2 fwd, port 2 bwd |
| `port` | 0 terminates the output, 1/2 select the source port; the reply carries `"source_on":true|false` |
| `src_freq` | synthesizer frequency, integer Hz in [50 MHz, 6 GHz] |
| `src_en` | `true`/`false`: the RF source as a whole (synthesizer, amplifier, attenuator) |

- The source boots powered up and parked at 1 GHz and stays running so its temperature is stable; `src_en:false` is
  the only way to turn it off, and the next sweep, generator start, `src_en:true` or `mode_exit` powers it up again.
- `port` 1/2 switches the source on only while it is enabled; otherwise the port is just remembered
  (`"source_on":false`). `port` 0 and `src_en:false` park a source that is on first.
- `lpf`, `cplr` and `src_freq` need the output terminated (`output_active`; use `port` 0 first).
- Impedance and attenuation are settings: `set`.

### `gen_start` / `gen_stop` — CW signal generator (mode `generator`)
```
→ {"cmd":"gen_start","id":15,"port":1,"z":50,"atten":40,"f":1000000000}
← {"id":15,"ok":true}
→ {"cmd":"gen_stop","id":16}
← {"id":16,"ok":true}
```
`port` 1|2, `z` 50|75, `atten` 0..127, `f` integer Hz in [50 MHz, 6 GHz]; all required. The device parks the
output, applies impedance, attenuation, LPF and frequency, waits up to 2 s for PLL lock and only then routes the
source to `port`. On `source_not_locked` / `tune_failed` the output stays terminated and the generator is off. A
`gen_start` while on re-applies everything (the output is briefly terminated). `gen_stop` terminates the output
(attenuator 127, port switch terminated); the synthesizer keeps running. The mode stays `generator` until
`mode_exit`. `set` changes impedance and level of a running output live.

### `power_read` — external power at a port (mode `power_meter`)
```
→ {"cmd":"power_read","id":17,"port":2,"z":50}
← {"id":17,"ok":true,"port":2,"z":50,"raw":1480,"volts":1.193,"dbm":-9.6,"range":"ok"}
```
Terminates the source, selects the backward-wave coupler path of `port` (the one S11/S22 read as the reflected
wave), waits the detector settle time and returns the median of 16 ADC samples. `port` and `z` are required.
`dbm` is approximate and **uncalibrated** (AD8318 datasheet slope/intercept at 900 MHz, referenced to the detector
input; coupler, switch and 75 Ω path losses and frequency dependence are not included). `range` is `ok`, `low`
(below about -55 dBm at the detector, unreliable) or `high` (above about 0 dBm, clipped; the port tolerates
+10 dBm at most). `busy` while the continuous power meter runs.

### `power_start` / `power_stop` — continuous power meter (mode `power_meter`)
```
→ {"cmd":"power_start","id":19,"port":2,"z":50,"interval_ms":250}
← {"id":19,"ok":true,"port":2,"z":50,"interval_ms":250}
← {"event":"power","port":2,"z":50,"raw":1480,"volts":1.193,"dbm":-9.6,"range":"ok"}
← {"event":"power",...}                       (one every interval_ms)
→ {"cmd":"power_stop","id":20}
← {"id":20,"ok":true}
```
`power_start` sets the input up once like `power_read`, then the device sends a `power` event every `interval_ms`
(optional, 20–10000, default 250) with the `power_read` fields; the first one right away. Another `power_start`
switches a running measurement to the new settings. `power_stop` ends the events, the mode stays `power_meter`;
`mode_exit` (and so a USB disconnect) ends them as well.

### `probe_read` — detector readout on the coupler paths, for troubleshooting (mode `generator` or `manual`)
```
→ {"cmd":"probe_read","id":18,"paths":[0,1,2,3]}
← {"id":18,"ok":true,"gen_on":true,"port":1,"z":50,"atten":40,"f":1000000000,
   "readings":[{"path":0,"name":"p1_fwd","raw":1500,"min":1495,"max":1505,"volts":1.209,"dbm":-9.5,"range":"ok"}, ...],
   "derived":{"refl":"s11","refl_db":-10.0,"refl_cal_db":-15.0,"trans":"s21","trans_db":-20.0,"trans_cal_db":-25.0,"cal":true}}
```
`paths` (optional, default all four, no duplicates) uses the `manual cplr` numbering. The route is left as it is:
with the generator on the source stays on with its port, impedance, level and frequency; otherwise the output is
terminated and the readings show leakage and the noise floor. Each path is selected, settled and sampled 16 times:
`raw` is the median, `min`/`max` the extremes, `dbm` and `range` as in `power_read`. `f` only while `gen_on`.
With the generator on and the three paths its source port needs, `derived` gives the pair that port measures
(port 1: S11 and S21, port 2: S22 and S12): `*_db` is the raw ratio, `*_cal_db` has the stored calibration applied,
`cal` says whether a correction existed for both. The coupler path is not restored afterwards.

### `src_temp_read` — synthesizer temperature sensor
```
→ {"cmd":"src_temp_read","id":14}
← {"id":14,"ok":true,"cached":false,"temp_c":42.3}
  (or ← {"id":14,"ok":false,"error":"not_valid"})
```
The read rewrites synthesizer registers, so during a sweep or with the source on the last live value is returned
instead: `"cached":true,"age_ms":N` (`not_valid` if none exists yet). The sensor tracks the VCO state; only trust it
at a fixed frequency.

### `bootsel` — reboot into the RP2350's UF2 bootloader
```
→ {"cmd":"bootsel","id":13}
← {"id":13,"ok":true}
```
The mode is cancelled to `idle` first; the device then disconnects and re-enumerates as a UF2 drive.

## Unsolicited events

- `power`: continuous power meter readings, see `power_start`.
- `sdi_test_result`, after a front-panel SDI test while a host is connected:
```
← {"event":"sdi_test_result","pass":false,"results":[{"f":270000000,"s_param":"s11","val_db":-16.1,"pass":true}, ...]}
```

## Front panel

- **BOOTED** blinks fast while the device initializes, then gives one short pulse per second while the firmware runs.
- **Init error:** BOOTED stays off and the red SDI LED repeats a blink code for every failed boot check, lowest first:
  N short blinks, then a pause. 1 = EEPROM, 2 = synthesizer lock, 3 = detector, 4 = USB. `status.init_errors`
  reports the same (except USB). The SDI button is ignored then.
- **READY/BUSY:** the load of both cores as brightness (share of time working, averaged over the two, linear PWM
  duty cycle, updated every 1 ms): BUSY's duty is the load, READY's the rest. Idle = READY only, 50 % load = both
  at 50 %, full load = BUSY only. A sweep keeps core 1 busy, so it shows as at least half load.
- **SDI test button:** runs the stored SDI points (from `idle` only; one short red blink otherwise). Without stored
  points (no `sdi_test_set` yet, or cleared) a press only gives one short orange blink (green and red together),
  checked before the idle check; nothing is measured. Otherwise: the same measurement as `sweep` at `atten` 0 (the level calibration curves are captured at, left
  as the setting) and the current `z`, strict lock, calibrated if data exists. Then one green (pass) or red (fail)
  blink per point in order, once. Runs with or without a host; with one connected the device also sends
  `sdi_test_result`. `abort`/`mode_exit` cancel it (no result then).

## Data export (client-side, not on the wire)

Sweep results are saved client-side as Touchstone (`.s1p`/`.s2p`) for interop with other RF tools, plus a native JSON format mirroring the `sweep` response shape for round-tripping in the app.
