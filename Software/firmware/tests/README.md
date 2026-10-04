# Host firmware regression tests

From `firmware/`, run `./tests/run-host-tests.ps1` with a host GCC compiler.
This compiles the actual main loop's CDC reader, protocol handlers, RF control,
EEPROM storage and EEPROM HAL against simulated hardware. It does not access
USB devices, change the ARM build configuration or flash firmware.

Checks cover every nonempty S-parameter combination with a nonreciprocal DUT,
source GPIO/LED state at every detector reading, calibration application,
Abort/status during a sweep, command isolation, disconnects, stalled output,
lock failures, full-sweep profiling phase accounting, long-wait cancellation,
invalid inputs, EEPROM errors, SDI without USB, oversized lines,
6 GHz calibration storage, page splitting and short I2C transfers.

The chip HALs in the RF test are simulated: tests cannot establish actual PLL
frequency, GPIO polarity/wiring, detector accuracy, switch settling or RF losses.

Run from `firmware/` with a host GCC compiler (not `arm-none-eabi-gcc`):

```powershell
gcc -std=c11 -Wall -Wextra -Werror -I src tests/eeprom_cal_test.c src/eeprom.c -lm -o build3/eeprom_cal_test.exe
if ($LASTEXITCODE -eq 0) { .\build3\eeprom_cal_test.exe }
```

The EEPROM HAL uses RAM. This test checks all eight calibration tables up to 6 GHz,
interpolation across the 32-bit boundary, a grid entirely above that boundary, persistence,
shared-grid rejection, scratch-area isolation, clearing, and invalidation of format 1 data.
It does not flash or access a device.
