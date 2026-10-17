$ErrorActionPreference = 'Stop'
Push-Location (Join-Path $PSScriptRoot '..')
try {
    New-Item -ItemType Directory -Path build-host-tests -Force | Out-Null
    $hostFlags = @('-std=c11', '-Wall', '-Wextra', '-Werror', '-D__USE_MINGW_ANSI_STDIO=1', '-I', 'tests/stubs', '-I', 'src', '-I', 'boards', '-I', 'src/third_party/cJSON', '-DCJSON_NESTING_LIMIT=8')
    & gcc @hostFlags '-Dmain=firmware_entry_point' -c src/main.c -o build-host-tests/main.o
    if ($LASTEXITCODE -ne 0) { throw 'Host main compilation failed' }
    & gcc @hostFlags tests/rf_protocol_test.c tests/stubs/queue_stub.c src/rf_task.c src/sdi_test.c src/measurement.c src/rf_control/source.c src/rf_control/coupler.c src/rf_control/detector.c src/rf_control/ports.c src/rf_control/state.c src/rf_control/application.c src/protocol.c src/eeprom.c src/third_party/cJSON/cJSON.c build-host-tests/main.o -lm -o build-host-tests/rf_protocol_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'RF/protocol test compilation failed' }
    & gcc @hostFlags tests/eeprom_cal_test.c src/eeprom.c -lm -o build-host-tests/eeprom_cal_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Calibration test compilation failed' }
    & gcc @hostFlags tests/eeprom_hal_test.c src/hal/eeprom_24aa32a.c -o build-host-tests/eeprom_hal_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'EEPROM HAL test compilation failed' }
    & gcc @hostFlags tests/rf_task_test.c tests/stubs/queue_stub.c src/rf_task.c -lm -o build-host-tests/rf_task_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Two-core rf_task test compilation failed' }
    & gcc @hostFlags tests/max2871_test.c src/hal/max2871.c -lm -o build-host-tests/max2871_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'PLL register test compilation failed' }
    foreach ($testName in @('rf_protocol_test', 'eeprom_cal_test', 'eeprom_hal_test', 'rf_task_test', 'max2871_test')) {
        & ".\build-host-tests\$testName.exe"
        if ($LASTEXITCODE -ne 0) { throw "$testName failed" }
    }
} finally {
    Pop-Location
}
