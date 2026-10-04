# Deliberately NOT including the Raspberry Pi Pico VS Code extension's
# auto-injected "pico-vscode.cmake" stanza here (it re-added itself at some
# point and was removed again, 2026-10-01) -- it unconditionally points
# PICO_SDK_PATH at the extension's own globally-managed SDK copy
# (~/.pico-sdk/sdk/<version>), which has a different internal CMake layout
# than the 2.2.0 tree this project actually pins (.toolchain/pico-sdk, see
# README.md), and overrides it on *every* reconfigure (not just once), not
# just when the cache happens to be empty. This project's actual workflow is
# command-line/PowerShell (see README.md's Building section), never the VS
# Code extension's own build button -- if that ever changes, re-add the
# extension's stanza deliberately rather than letting it silently reappear.
cmake_minimum_required(VERSION 3.13)

# Custom board: RP2350B (QFN, 48 GPIO) SNA hardware -- not a stock Pico/Pico 2.
# See boards/sna.h and Software/protocol/PINMAP.md.
set(PICO_BOARD_HEADER_DIRS ${CMAKE_CURRENT_LIST_DIR}/boards)
set(PICO_BOARD sna CACHE STRING "Board type")
set(PICO_PLATFORM rp2350-arm-s CACHE STRING "Platform")

include(pico_sdk_import.cmake)

project(sna_firmware C CXX ASM)

set(CMAKE_C_STANDARD 11) # pico-sdk 2.x headers use static_assert(), needs C11+
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

# Vendored third-party JSON library (see src/third_party/cJSON/README.md) --
# its own target, deliberately NOT given our -Wall -Wextra below, so
# "zero warnings" stays a statement about our own code, not noise from code
# we didn't write.
add_library(cjson STATIC src/third_party/cJSON/cJSON.c)
target_include_directories(cjson PUBLIC src/third_party/cJSON)

add_executable(sna_firmware
    src/main.c
    src/protocol.c
    src/eeprom.c
    src/rf_control.c
    src/acquisition.c
    src/usb_descriptors.c
    # src/hal/ -- one driver module per chip; see each header for the part
    # it drives. src/rf_control.c and src/eeprom.c compose these into the
    # board-level/application-facing API, they don't talk to GPIO/I2C/ADC
    # registers directly anymore.
    src/hal/max2871.c
    src/hal/ad8318.c
    src/hal/trf37a73.c
    src/hal/hmc1119.c
    src/hal/rf_switch.c
    src/hal/eeprom_24aa32a.c
)

target_compile_options(sna_firmware PRIVATE -Wall -Wextra)
target_compile_definitions(sna_firmware PRIVATE SNA_MULTICORE=1 PICO_CORE1_STACK_SIZE=0x1000)
option(SNA_PLL_CP20_EXPERIMENT "Use hardware-validated LibreVNA CP20 linearity mode; keep CP current unchanged" ON)
if(SNA_PLL_CP20_EXPERIMENT)
    target_compile_definitions(sna_firmware PRIVATE SNA_PLL_CP20_EXPERIMENT=1)
endif()

# Bring-up safety gate: while ON (the default), max2871_enable() in
# src/hal/max2871.c never actually drives CE/RFOUT_EN high, no matter what
# the application layer asks for -- it only lets the SPI-register-write path
# (max2871_set_freq) be exercised. Per feedback_sna_bringup_priority (project
# memory): of all the RF blocks, the MAX2871 is the one genuinely risky one
# and should be brought up in isolation with lock-detect checked, not folded
# into general testing. Turn this OFF only once that isolated bring-up step
# has actually happened.
option(SNA_BRINGUP_NO_RF_SOURCE "Never actually enable the MAX2871 (CE/RFOUT_EN stay low)" ON)
if(SNA_BRINGUP_NO_RF_SOURCE)
    target_compile_definitions(sna_firmware PRIVATE SNA_BRINGUP_NO_RF_SOURCE=1)
endif()

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
    tinyusb_device
    cjson
)

# We talk to the host over a hand-rolled TinyUSB CDC protocol loop (see
# src/protocol.c), not the SDK's printf-over-USB convenience wrapper.
pico_enable_stdio_usb(sna_firmware 0)
pico_enable_stdio_uart(sna_firmware 0)

pico_add_extra_outputs(sna_firmware)
