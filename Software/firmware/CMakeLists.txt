# Don't add the Raspberry Pi Pico VS Code extension's "pico-vscode.cmake"
# stanza here: it points PICO_SDK_PATH at the extension's own SDK copy
# (~/.pico-sdk/sdk/<version>) on every reconfigure, overriding the Pico SDK
# 2.2.0 that devenv pins (see README.md, "Building").
cmake_minimum_required(VERSION 3.13)

# Custom board: RP2350B (QFN, 48 GPIO) SNA hardware -- not a stock Pico/Pico 2.
# See boards/sna.h and Software/docs/firmware/protocol/PINMAP.md.
set(PICO_BOARD_HEADER_DIRS ${CMAKE_CURRENT_LIST_DIR}/boards)
set(PICO_BOARD sna CACHE STRING "Board type")
set(PICO_PLATFORM rp2350-arm-s CACHE STRING "Platform")

# compile_commands.json in the build dir is what VS Code IntelliSense reads
# (.vscode/c_cpp_properties.json / C_Cpp.default.compileCommands).
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(pico_sdk_import.cmake)

project(sna_firmware C CXX ASM)

set(CMAKE_C_STANDARD 11) # pico-sdk 2.x headers use static_assert(), needs C11+
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

# Vendored cJSON (see src/third_party/cJSON/README.md): its own target without
# our -Wall -Wextra, so "zero warnings" applies to our own code only.
add_library(cjson STATIC src/third_party/cJSON/cJSON.c)
target_include_directories(cjson PUBLIC src/third_party/cJSON)
# Real requests nest 2-3 levels. cJSON's default limit (1000) lets one line of
# "[[[[..." recurse core 0's 2 KB stack into core 1's stack (no stack guard).
target_compile_definitions(cjson PUBLIC CJSON_NESTING_LIMIT=8)

add_executable(sna_firmware
    src/main.c
    src/protocol.c
    src/eeprom.c
    src/rf_task.c
    src/measurement.c
    src/sdi_test.c
    src/usb_descriptors.c
    # Board-level RF control in three layers (src/rf_control/rf_control.h):
    # blocks (the only HAL callers), state, application.
    src/rf_control/source.c
    src/rf_control/coupler.c
    src/rf_control/detector.c
    src/rf_control/ports.c
    src/rf_control/state.c
    src/rf_control/application.c
    # One driver per chip; only src/rf_control/ and src/eeprom.c use them.
    src/hal/max2871.c
    src/hal/ad8318.c
    src/hal/trf37a73.c
    src/hal/hmc1119.c
    src/hal/rf_switch.c
    src/hal/eeprom_24aa32a.c
)

target_compile_options(sna_firmware PRIVATE -Wall -Wextra)
target_compile_definitions(sna_firmware PRIVATE PICO_CORE1_STACK_SIZE=0x1000)

target_include_directories(sna_firmware PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src
    ${CMAKE_CURRENT_LIST_DIR}/boards
)

target_link_libraries(sna_firmware PRIVATE
    pico_stdlib
    pico_multicore
    pico_util
    pico_unique_id
    pico_bootrom
    hardware_i2c
    hardware_adc
    hardware_gpio
    hardware_pwm
    hardware_watchdog
    tinyusb_device
    cjson
)

# The host protocol uses TinyUSB CDC directly (src/protocol.c), not SDK stdio.
pico_enable_stdio_usb(sna_firmware 0)
pico_enable_stdio_uart(sna_firmware 0)

pico_add_extra_outputs(sna_firmware)
