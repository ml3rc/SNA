# Host firmware regression tests

From `firmware/`, run `./tests/run-host-tests.ps1` (PowerShell, Windows) with a host GCC compiler.
It builds each test into `build-host-tests/` with `-Wall -Wextra -Werror` and runs them in turn.
These are unit tests of the real firmware sources against simulated hardware;
they do not access USB devices, change the ARM build or flash anything.
Hardware testing is done by flashing the complete firmware and using the client.

`rf_protocol_test.c` is split into one `test_*()` function per scenario. `main()` runs them in
order and they share the simulated board, so each scenario starts where the previous one left it.

| Test | Covers |
|---|---|
| `rf_protocol_test.c` | The whole firmware on one thread: `main.c` (CDC reader, boot), `protocol.c`, `rf_task.c`, `sdi_test.c`, `measurement.c`, `eeprom.c` and all of `rf_control/` with fake chip HALs and a fake USB link. `run()` plays both cores: it alternates core 0's line reader and `protocol_poll()` with core 1's `rf_task_step()` until nothing is left, so every scenario is deterministic. Boot self-test (lock and detector failures), every S-parameter combination with a nonreciprocal DUT, source/port/LED state at every detector reading, lock timing and unlocked fallback, abort/status/`set` between sweep points, busy isolation, `mode_exit` during a sweep, disconnect and stalled host, calibration, generator (incl. cancelled start), single and continuous power meter, probe, raw detector sweeps, temperature cache, modes, invalid inputs, oversized lines, SDI test with and without a host, `bootsel` |
| `rf_task_test.c` | The real core-1 loop (`rf_task.c`) on a second Win32 thread against a stub application layer that asserts every call comes from core 1: replies in order with their status, linear/log grids with exact endpoints, bounded backpressure, cancellation between points and inside a lock wait, startup/point failure, `continue_on_failure`, explicit grids, raw detector sweeps, what core 1 accepts between sweep points (`set`, `status`, `mode_exit` ending the sweep first, everything else busy) |
| `eeprom_cal_test.c` | Calibration storage with a RAM-backed EEPROM HAL: all eight tables up to 6 GHz, interpolation across the 32-bit boundary, persistence, shared-grid rejection, clearing, I2C failures, format migration |
| `eeprom_hal_test.c` | The 24AA32A driver: page splitting, short transfers, bounds |
| `max2871_test.c` | The real MAX2871 register output: write order, frequency range, charge pump and CP20 mode |

`tests/stubs/` holds the Pico SDK, TinyUSB and hardware headers the tests compile against.
`tests/stubs/queue_stub.c` implements `pico/util/queue.h` as a bounded FIFO behind a Windows critical
section, so the same queue works on one thread (`rf_protocol_test`) and between two (`rf_task_test`).

The chip HALs in the RF test are simulated: the tests cannot establish actual PLL
frequency, GPIO polarity/wiring, detector accuracy, switch settling or RF losses.
`rf_task_test` checks the inter-core protocol with real threads, but not the RP2350's
own queue implementation or timing.
