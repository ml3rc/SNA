$ErrorActionPreference = 'Stop'
Push-Location (Join-Path $PSScriptRoot '..')
try {
    New-Item -ItemType Directory -Path build-host-tests -Force | Out-Null
    $hostFlags = @('-std=c11', '-Wall', '-Wextra', '-Werror', '-D__USE_MINGW_ANSI_STDIO=1', '-I', 'tests/stubs', '-I', 'src', '-I', 'boards', '-I', 'src/third_party/cJSON')
    & gcc @hostFlags '-Dmain=firmware_entry_point' -c src/main.c -o build-host-tests/main.o
    if ($LASTEXITCODE -ne 0) { throw 'Host main compilation failed' }
    & gcc @hostFlags tests/rf_protocol_test.c src/rf_control.c src/protocol.c src/eeprom.c src/third_party/cJSON/cJSON.c build-host-tests/main.o -lm -o build-host-tests/rf_protocol_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'RF/protocol test compilation failed' }
    & gcc @hostFlags tests/eeprom_cal_test.c src/eeprom.c -lm -o build-host-tests/eeprom_cal_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Calibration test compilation failed' }
    & gcc @hostFlags tests/eeprom_hal_test.c src/hal/eeprom_24aa32a.c -o build-host-tests/eeprom_hal_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'EEPROM HAL test compilation failed' }
    & gcc @hostFlags tests/acquisition_test.c src/acquisition.c -lm -o build-host-tests/acquisition_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Concurrent acquisition test compilation failed' }
    & gcc @hostFlags tests/max2871_test.c src/hal/max2871.c -lm -o build-host-tests/max2871_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'PLL register test compilation failed' }
    & gcc @hostFlags '-DSNA_PLL_CP20_EXPERIMENT=1' tests/max2871_test.c src/hal/max2871.c -lm -o build-host-tests/max2871_cp20_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'CP20 register test compilation failed' }
    foreach ($testName in @('rf_protocol_test', 'eeprom_cal_test', 'eeprom_hal_test', 'acquisition_test', 'max2871_test', 'max2871_cp20_test')) {
        & ".\build-host-tests\$testName.exe"
        if ($LASTEXITCODE -ne 0) { throw "$testName failed" }
    }
} finally {
    Pop-Location
}
