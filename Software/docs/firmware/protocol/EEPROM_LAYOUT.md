# EEPROM layout (U203, Microchip 24AA32A, 32Kbit = 4096 bytes, I2C)

All calibration and persistent config lives in this one EEPROM — no flash filesystem needed: the 4 KB budget works out because the 8 calibration tables share one implicit frequency grid and store one `int16` correction value per point, rather than frequencies per point or raw O/S/L/Thru terms.

**format_version 3** (firmware 0.4.0): SDI test-point frequencies widened from `u32` to `u64`, so SDI points cover the whole 50 MHz–6 GHz range (12G-SDI). The SDI block grew from 128 to 188 bytes, which moved the calibration tables. `eeprom_init()` treats any stored `format_version != 3` like a blank EEPROM: it writes a fresh header and empty calibration grid and SDI config. **Updating from format 2 erases calibration and SDI points; recalibrate afterwards.**

All multi-byte values are little-endian, structures packed.

| Offset | Size | Contents |
|---|---|---|
| `0x000` | 32 B | **Device header**: magic `"SNA1"` (4B), format version (1B, 3), hw revision (1B, `ping.hw_rev`), serial number (2B), reserved (24B) |
| `0x020` | 24 B | **Cal grid**: `cal_f0_hz` (u64), `cal_f1_hz` (u64), `cal_points` (u16, 240 when any set is stored), `cal_valid_mask` (u8, bit per set), reserved (5B) |
| `0x038` | 188 B | **SDI test config**: `point_count` (u8), reserved (7B), then up to 15 × {`f_hz` (u64), `thresh_db_x100` (i16), `cmp` (u8, 0=lt/1=gt), `s_param` (u8, 0=s11/1=s21/2=s12/3=s22)} = 12 B each |
| `0x0F4` | 3840 B | **8 calibration tables**, 480 B each, fixed order: `s11_50, s21_50, s12_50, s22_50, s11_75, s21_75, s12_75, s22_75`. Each table = 240 × `int16` (0.01 dB), indexed 0..239 evenly across `[cal_f0_hz, cal_f1_hz]`. |

Total used: 4084 / 4096 bytes (12 bytes spare).

**Fixed-point encoding:** all stored dB values are `int16` in units of 0.01 dB (e.g. `-12.34 dB` → `-1234`). Range ±327.67 dB.

**Validity:** a calibration grid is only used within 50 MHz–6 GHz with `cal_f0_hz <= cal_f1_hz` and `cal_points == 240`. An SDI config is only used if every point is within 50 MHz–6 GHz with valid `cmp`/`s_param`; otherwise it reads as empty.

**Interpolation:** a sweep rarely lands exactly on the 240-point grid. The firmware interpolates linearly between the two nearest grid points for each sweep point. Sweep points outside `[cal_f0_hz, cal_f1_hz]` are returned uncorrected with `"cal":false`.

**Why 240 points:** 3840 B ÷ 8 sets ÷ 2 B/point = 240, the practical ceiling for this EEPROM. The `sweep` command itself accepts any point count, interpolated against the stored grid.
