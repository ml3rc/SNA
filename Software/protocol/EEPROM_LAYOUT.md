# EEPROM layout (U203, Microchip 24AA32A, 32Kbit = 4096 bytes, I2C)

All calibration and persistent config lives in this one EEPROM — no flash filesystem needed (see project memory for why: the 4KB budget works out if the 8 calibration tables share one implicit frequency grid and store one `int16` correction value per point, rather than storing frequencies per-point or multiple raw O/S/L/Thru terms).

**format_version 2** (bumped from 1, 2026-10-03): `cal_f0_hz`/`cal_f1_hz` widened from `u32` to `u64`. A `u32` can only hold up to ~4.295 GHz, but this device sweeps up to 6 GHz — confirmed on real hardware that calibrating with `f1` above that limit silently corrupted the stored upper bound (the firmware's JSON-to-u32 cast for a 6,000,000,000 Hz value overflows, and saturates to `0xFFFFFFFF` on this target), which then fed completely wrong frequency-to-table-index math in `eeprom_interpolate()`, producing physically-impossible corrected S-parameters for any calibration set that had been captured near/above ~4.3 GHz. `eeprom_init()` treats any stored `format_version != 2` the same as blank/uninitialized EEPROM and wipes it, so a device updated from an older firmware starts with a clean slate under the new layout rather than misreading old-layout bytes — **recalibration is required after this update**.

| Offset | Size | Contents |
|---|---|---|
| `0x000` | 32 B | **Device header**: magic `"SNA1"` (4B), data-format version (1B, currently 2), hw revision (1B), serial number (2B), reserved (24B) |
| `0x020` | 24 B | **Cal grid config**: `cal_f0_hz` (u64), `cal_f1_hz` (u64), `cal_points` (u16), `cal_valid_mask` (u8, one bit per set = calibrated or not), reserved (5B) |
| `0x038` | 128 B | **SDI test config**: `point_count` (u8), reserved (7B), then up to 15 × {`f_hz` (u32), `thresh_db_x100` (i16), `cmp` (u8, 0=lt/1=gt), `s_param` (u8, 0=s11/1=s21/2=s12/3=s22)} = 8B each |
| `0x0B8` | 3840 B | **8 calibration tables**, 480 B each, fixed order: `s11_50, s21_50, s12_50, s22_50, s11_75, s21_75, s12_75, s22_75`. Each table = 240 × `int16` (units: 0.01 dB), indexed 0..239 evenly across `[cal_f0_hz, cal_f1_hz]` from the cal grid config above. |

Total used: 4024 / 4096 bytes (72 bytes spare).

Firmware 0.1.9 accepts new calibration grids only within 50 MHz–6 GHz.
Format 2 and table offsets are unchanged. Previously stored format-2 grids
starting at 23.5 MHz remain readable and interpolate on their original grid
for measurements at or above 50 MHz. They are not rescaled or automatically
cleared. New and standalone SDI points must be at least 50 MHz; an existing
SDI configuration containing lower points must be replaced before running it.

**SDI range limit:** SDI test-point `f_hz` remains `u32`. Firmware 0.1.1 rejects SDI
threshold frequencies above UINT32_MAX Hz rather than allowing an overflowing write.
Calibration supports the full 6 GHz range. A future full-range SDI representation must
fit the remaining EEPROM budget and define compatibility explicitly.

**Fixed-point encoding:** all stored dB values are `int16` in units of 0.01 dB (e.g. `-12.34 dB` → `-1234`). Range ±327.67 dB, ample headroom.

**Interpolation:** a live `sweep` request rarely lands exactly on the 240-point cal grid. Firmware linearly interpolates between the two nearest cal-grid points for each requested sweep point. Sweep points outside `[cal_f0_hz, cal_f1_hz]` are returned uncorrected with `"cal":false`.

**Why 240 points, not more:** 3840 B ÷ 8 sets ÷ 2 B/point = 240. Matches the `cal_points` field's storage budget exactly; this is the practical ceiling for the current EEPROM, not a protocol limit — the `sweep` command itself accepts any point count, interpolated against whatever grid is actually stored.
