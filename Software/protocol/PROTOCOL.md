# SNA host protocol

Single source of truth for the firmware ↔ client wire protocol. Both `firmware/` and `client/` implement against this document — update it first, then both sides.

## Transport

USB CDC-ACM (virtual serial port), 115200 baud nominal (irrelevant over USB CDC but set for compatibility). Full-Speed USB (12 Mbit/s), no driver install needed on Windows/Linux/macOS; Android via `usb-serial-for-android`.

## Framing

One JSON object per line, terminated by `\n`. PC → device is always a `{"cmd": ..., "id": N, ...}` request. Device → PC is either:
- a **response** to a request, echoing the same `"id"`, always containing `"ok": true|false` (`"error": "..."` when false), or
- a **stream** of intermediate results during a multi-line operation (e.g. sweep points), also echoing `"id"`, followed by a final response with `"done": true`, or
- an **unsolicited event**, using `"event"` instead of `"id"`/`"cmd"` (e.g. the standalone SDI self-test result when a PC happens to be connected).

`id` is chosen by the client (any integer/string), used purely for request/response correlation — the device does not interpret it.

## Commands

### `ping`

Firmware 0.1.9 reports `freq_min_hz:50000000` and `freq_max_hz:6000000000`.
The supported analyzer range is 50 MHz–6 GHz, including manual source control
and new calibration grids. Existing format-2 calibration grids starting below
50 MHz remain readable and applicable within the supported band; they are not
automatically cleared. Clear all calibration explicitly before recapturing such
a grid with the new lower bound. SDI frequencies remain limited to UINT32_MAX Hz
by their storage representation, with a new minimum of 50 MHz.
```
→ {"cmd":"ping","id":1}
← {"id":1,"ok":true,"fw":"0.1.0","hw_rev":1,"proto":1}
```

### `status`
```
→ {"cmd":"status","id":2}
← {"id":2,"ok":true,"state":"idle|sweeping|error","z":50,"source_port":1,"last_error":null}
```

### `set_z` — select 50Ω/75Ω mode
```
→ {"cmd":"set_z","id":3,"z":50}
← {"id":3,"ok":true}
```

### `sweep` — trigger a frequency sweep, results streamed as measured
```
→ {"cmd":"sweep","id":4,"f0":50000000,"f1":6000000000,"points":201,"log":false,"z":50,"params":["s11","s21"]}
← {"id":4,"pt":0,"f":50000000,"s11_db":-12.3,"s21_db":-0.5}
← {"id":4,"pt":1,"f":29850000,"s11_db":-11.9,"s21_db":-0.4}
  ...
← {"id":4,"ok":true,"done":true}
```
`params` is a nonempty subset of `["s11","s21","s12","s22"]`. S11/S21 use source port 1; S12/S22 use source port 2. The detector can read either port without changing the source. Reference/response readings are captured separately for each source state. Requesting both excitation directions adds a source change and separate reference acquisition per frequency point. If calibration data exists for the requested `z` and covers the requested range, values are already corrected (device interpolates its stored cal grid onto the requested sweep grid); otherwise raw uncorrected dB values are returned and `"cal":false` is added to each point.

Sweep frequencies must be integer Hz in [50000000, 6000000000], with `f0 <= f1`.
`points` must be an integer in [1,4096]; `z` is 50 or 75 (default 50).
Invalid inputs are rejected before RF is enabled. In firmware 0.1.5, normal sweeps
default to `allow_unlocked:true`: after the bounded wait, actual detector values
are returned even if stable LD was not observed. These points have `locked:false`,
`measured:true`, and `cal:false`; they are provisional readings, not confirmed
measurements at the requested RF frequency. The trace continues and completion
is `ok:true` with `warning:"source_unlocked"` and `unlocked_points`. Abort,
disconnect and tuning/programming errors still stop acquisition.

Set `allow_unlocked:false` for strict acquisition. Calibration captures and
verification in client 1.1.4 request this explicitly. SDI tests stay strict.
Firmware 0.1.6 also accepts optional `atten` (integer 0..127, in 0.25 dB steps)
for a single sweep. It saves the previous attenuation, applies the override
before enabling RF, and restores the previous setting after RF shutdown on
completion, failed startup, Abort or disconnect. Without `atten`, existing level
is preserved. Client 1.1.5 requests `atten:0` for every calibration capture and
verification, using the maximum programmable source level. `ping.sweep_atten`
advertises support; the client rejects this operation on older firmware.
`status.atten` reports the live attenuator code. This is a relative level control,
not calibrated absolute power at the test port.
Strict failure to lock terminates the stream with
`{"id":4,"ok":false,"done":true,"error":"source_not_locked","stage":"retune","pt":12,"f":5100000000}` instead of zero-valued points.
Firmware 0.1.2 allows up to 20 ms per retune and requires LD high for 100 us.
`stage` distinguishes `startup` (initial lock, up to two seconds) from `retune`.
The timeout is a fallback ceiling; normally acquisition resumes earlier.
Host disconnect or a USB write stalled for two seconds ends the measurement and disables RF.

Firmware 0.1.10 advertises `ping.sweep_raw_adc:true`. Optional `raw_adc:true`
adds a `raw_adc` object to each measured sweep point with uncalibrated 12-bit
detector counts: `source1_ref`, `source1_reflection`, `source1_transmission`,
`source2_ref`, `source2_reflection`, `source2_transmission`. Only readings needed
by the requested parameters are included. Normal dB results and EEPROM
calibration remain unchanged. This is a diagnostic readout, not absolute dBm.

### `abort`
```
→ {"cmd":"abort","id":5}
← {"id":5,"ok":true}
```

Abort is serviced between frequency points during a sweep. `ping` and `status` are also
available; other commands receive `busy` until the measurement ends. An Abort also stops
a running front-panel demo. Lock waits service cancellation approximately every
millisecond. `status.source_port` reports the selected source port (1 or 2),
including while idle; it does not assert that RF output is enabled.

### `cal_set` / `cal_get` / `cal_clear` — calibration data, one of 8 sets
Set names: `s11_50`, `s21_50`, `s12_50`, `s22_50`, `s11_75`, `s21_75`, `s12_75`, `s22_75`.

All 8 sets share one frequency grid (device storage constraint — see `EEPROM_LAYOUT.md`). The first `cal_set` after a `cal_clear all` establishes `f0`/`f1`/`points` for the grid; subsequent sets must match or the device returns `"error":"grid_mismatch"`.
```
→ {"cmd":"cal_set","id":6,"set":"s11_50","f0":50000000,"f1":6000000000,"points":[<float dB, len N>]}
← {"id":6,"ok":true}

→ {"cmd":"cal_get","id":7,"set":"s11_50"}
← {"id":7,"ok":true,"set":"s11_50","f0":50000000,"f1":6000000000,"points":[...]}
  (or ← {"id":7,"ok":false,"error":"not_calibrated"})

→ {"cmd":"cal_clear","id":8,"set":"all"}
← {"id":8,"ok":true}
```
The client computes the actual correction curve from raw Open/Short/Load/Thru sweeps (SLOT calibration) and uploads the *result* — the device only stores/interpolates/applies a single correction curve per set, it does not know about O/S/L/Thru standards.

### `sdi_test_set` / `sdi_test_get` — configure the standalone front-panel SDI test
```
→ {"cmd":"sdi_test_set","id":9,"points":[{"f":270000000,"thresh_db":-15,"s_param":"s11","cmp":"lt"}, {"f":1485000000,"thresh_db":-15,"s_param":"s21","cmp":"lt"}, {"f":2970000000,"thresh_db":-12,"s_param":"s11","cmp":"lt"}]}
← {"id":9,"ok":true}

→ {"cmd":"sdi_test_get","id":10}
← {"id":10,"ok":true,"points":[...]}
```
`s_param` is one of `"s11"`/`"s21"`/`"s12"`/`"s22"` — which S-parameter this threshold point evaluates (e.g. return loss via S11/S22, insertion loss via S21/S12). `cmp` is `"lt"` (measured value must be less than threshold — e.g. return loss dB) or `"gt"`. On a physical button press, firmware runs a sweep restricted to these points (same measurement path as `sweep`, cal-corrected if available), measures each point's configured S-parameter, evaluates it against its threshold, and blinks the pass/fail LEDs in sequence (one green/red blink per point in order, then a pause, then repeat) — independent of whether a PC is connected. If a PC *is* connected when the button is pressed, the device also emits:
```
← {"event":"sdi_test_result","pass":false,"results":[{"f":270000000,"val_db":-16.1,"pass":true}, ...]}
```

### `manual` — direct RF-block control, for bring-up/bench diagnostics
```
→ {"cmd":"manual","id":11,"block":"atten","value":15}
← {"id":11,"ok":true}
```
`block` ∈ `atten` (0–127), `lpf` (filter index), `cplr` (coupler-bank switch state), `port` (port select), `z` (impedance select, same as `set_z`), `src_freq` (Hz, drives MAX2871 directly), `src_en` (bool), `amp_en` (bool — RF-Attenuation/Amp stage amplifier power-down control). Exists so each RF block (source, filter bank, coupler bank, port switch, amp) can be exercised independently on the bench, per the project's "getrennt prüfbare RF-Blöcke" requirement — not used during normal sweep operation.

### `adc_read` — raw detector reading, for bring-up
```
→ {"cmd":"adc_read","id":12}
← {"id":12,"ok":true,"raw":2048,"volts":1.65}
```

### `src_temp_read` — MAX2871 internal temperature sensor, for bring-up/thermal monitoring
```
→ {"cmd":"src_temp_read","id":14}
← {"id":14,"ok":true,"temp_c":42.3}
  (or ← {"id":14,"ok":false,"error":"not_valid"} — e.g. the source isn't enabled)
```
Datasheet's own conversion formula; most accurate when the source is enabled and locked, still returned (less trustworthy) otherwise.

### `bootsel` — reboot into the RP2350's USB mass-storage (UF2) bootloader for firmware updates
```
→ {"cmd":"bootsel","id":13}
← {"id":13,"ok":true}
```
(device then disconnects and re-enumerates as a UF2 drive)

Calibration values and SDI thresholds must be finite and representable in 0.01 dB
signed 16-bit storage ([-327.68, 327.67] dB). Calibration grids use integer frequencies
through 6 GHz. The current SDI storage uses unsigned 32-bit Hz, so SDI threshold points
above UINT32_MAX Hz are rejected with `bad_points`; they are never truncated.
SDI comparison must be exactly `lt` or `gt`. Failed calibration clears and SDI writes
return `storage_error`. A failed overwrite of a calibration table leaves that set invalid.
The front-panel button runs configured SDI tests even without USB; with no configured points,
it toggles the hardware demo sweep. Protocol measurements take ownership from the demo.

## Data export (client-side, not on the wire)

Sweep results are saved client-side as Touchstone (`.s1p`/`.s2p`) for interop with other RF tools (incl. the LibreVNA comparison measurements), plus a native JSON format mirroring the `sweep` response shape for round-tripping in the app.
