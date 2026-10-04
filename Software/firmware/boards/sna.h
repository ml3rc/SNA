#ifndef _BOARDS_SNA_H
#define _BOARDS_SNA_H

// ============================================================================
// SNA custom board header (RP2350B, QFN package, 48 GPIO).
//
// Loosely modeled on the Pico SDK's stock board headers (e.g. boards/pico2.h)
// but trimmed to just what this board needs, since it isn't a Raspberry Pi
// reference board. Verify the SDK-required macros below against whatever
// pico-sdk version ends up installed -- board header conventions have moved
// around between SDK releases.
// ============================================================================

#define PICO_RP2350B 1

// --- boot flash (U201, W25Q128JVSIQ, 128Mbit) ---
#ifndef PICO_FLASH_SIZE_BYTES
#define PICO_FLASH_SIZE_BYTES (16 * 1024 * 1024)
#endif

// --- 12MHz crystal (Y201) ---
#ifndef PICO_XOSC_STARTUP_DELAY_MULTIPLIER
#define PICO_XOSC_STARTUP_DELAY_MULTIPLIER 64
#endif

// No UART header or on-board LED on this design -- host link is USB CDC only.
// Deliberately NOT defining PICO_DEFAULT_UART/_TX_PIN/_RX_PIN/_LED_PIN at all
// (not even as -1): hardware/uart.h token-pastes PICO_DEFAULT_UART directly
// into "uart##PICO_DEFAULT_UART" to form the uart_default macro, so -1
// produces an invalid "uart-1" token and fails to compile. Leaving these
// undefined is the SDK's own convention for boards with no default UART/LED
// (see src/boards/include/boards/none.h) -- code that references
// uart_default/PICO_DEFAULT_LED_PIN degrades via #ifdef, it doesn't error.

// ============================================================================
// Application pin map.
//
// Extracted from Altium/Project Outputs for SNA/SNA.NET, resolved through
// series/damping resistors, then converted from RP2350B QFN-80 *package pin*
// numbers to logical GPIO numbers using the official RP2350 datasheet's own
// pinout figure (Figure 3, section 1.2.1.2 "QFN-80 (RP2350B)") -- rendered
// and read directly off the diagram, then cross-checked against a second
// independent source (rp2350b.pinout.xyz) which matched exactly. Not a
// placeholder -- see Software/protocol/PINMAP.md for the full annotated
// pin<->GPIO<->signal table and the cross-check notes. Regenerate PINMAP.md
// (and this header) from the netlist if the schematic changes; the package
// pin -> GPIO mapping itself is fixed silicon and never changes.
// ============================================================================

// --- MAX2871 RF source (4-wire, bit-banged -- see rf_control.c) ---
#define GPIO_RF_SRC_CLK        38  // pkg pin 47
#define GPIO_RF_SRC_DATA       39  // pkg pin 48
#define GPIO_RF_SRC_LE         37  // pkg pin 46
#define GPIO_RF_SRC_CE         35  // pkg pin 44
#define GPIO_RF_SRC_MUX        36  // pkg pin 45 (readback/muxout)
#define GPIO_RF_SRC_LD         34  // pkg pin 43 (lock detect, input)
#define GPIO_RF_SRC_RFOUT_EN   40  // pkg pin 49 (ADC0-capable, used digital)

// --- Digital attenuator (7-bit + latch enable) ---
#define GPIO_ATTEN_D0           23  // pkg pin 23, LSB
#define GPIO_ATTEN_D1           22  // pkg pin 22
#define GPIO_ATTEN_D2           21  // pkg pin 21
#define GPIO_ATTEN_D3           20  // pkg pin 20
#define GPIO_ATTEN_D4           19  // pkg pin 19
#define GPIO_ATTEN_D5           18  // pkg pin 18
#define GPIO_ATTEN_D6           17  // pkg pin 17, MSB
#define GPIO_ATTEN_LE            24  // pkg pin 25

// --- LPF bank: 2 switches, 2 ctrl lines each ---
#define GPIO_SW_LPF1_C1          42  // pkg pin 53 (ADC2-capable, used digital)
#define GPIO_SW_LPF1_C2          41  // pkg pin 52 (ADC1-capable, used digital)
#define GPIO_SW_LPF2_C1          44  // pkg pin 55 (ADC4-capable, used digital)
#define GPIO_SW_LPF2_C2          43  // pkg pin 54 (ADC3-capable, used digital)

// --- Directional coupler bank: 6 SP4T switches, 2 ctrl lines each ---
#define GPIO_SW_CPLR1_C1         3   // pkg pin 80
#define GPIO_SW_CPLR1_C2         2   // pkg pin 79
#define GPIO_SW_CPLR2_C1         5   // pkg pin 2
#define GPIO_SW_CPLR2_C2         4   // pkg pin 1
#define GPIO_SW_CPLR3_C1         7   // pkg pin 4
#define GPIO_SW_CPLR3_C2         6   // pkg pin 3
#define GPIO_SW_CPLR4_C1         9   // pkg pin 7
#define GPIO_SW_CPLR4_C2         8   // pkg pin 6
#define GPIO_SW_CPLR5_C1         11  // pkg pin 9
#define GPIO_SW_CPLR5_C2         10  // pkg pin 8
#define GPIO_SW_CPLR6_C1         13  // pkg pin 12
#define GPIO_SW_CPLR6_C2         12  // pkg pin 11

// --- Port-select switch ---
#define GPIO_SW_PORT_C1          15  // pkg pin 14
#define GPIO_SW_PORT_C2          16  // pkg pin 16

// --- 50/75 ohm impedance switch ---
#define GPIO_SW_Z_C1             1   // pkg pin 78
#define GPIO_SW_Z_C2             0   // pkg pin 77

// --- RF log detector ---
#define GPIO_DET_EN              46  // pkg pin 57 (ADC6-capable, used digital)
#define GPIO_DET_OUT_ADC         45  // pkg pin 56 -- GPIO45/ADC5, genuinely analog: this is the onboard ADC input
#define ADC_CHANNEL_DET          5   // RP2350 ADC channel = GPIOxx - 40, i.e. GPIO45 -> channel 5

// --- I2C0 (EEPROM, U203) ---
#define GPIO_I2C_SDA             32  // pkg pin 40
#define GPIO_I2C_SCL             33  // pkg pin 42
#define I2C_PORT                 i2c0
#define I2C_BAUD                 400000
#define EEPROM_I2C_ADDR          0x50  // 24AA32A default 7-bit address (A0-A2 tied GND)

// --- SDI front-panel self-test ---
#define GPIO_SDI_TEST_BUTTON     25  // pkg pin 26, active-low, pulled up
#define GPIO_SDI_TEST_PASS_LED   26  // pkg pin 27, green
#define GPIO_SDI_TEST_FAIL_LED   27  // pkg pin 28, red

// --- Status LEDs ---
#define GPIO_LED_PORT1_OUT_EN    28  // pkg pin 36
#define GPIO_LED_PORT_Z50        29  // pkg pin 37 -- polarity may be inverted vs. the 75ohm indicator per schematic notes -- verify before use
#define GPIO_LED_READY           30  // pkg pin 38
#define GPIO_LED_BUSY            31  // pkg pin 39
#define GPIO_LED_BOOTED          47  // pkg pin 58 (ADC7-capable, used digital)

// --- RF-Attenuation/Amp amplifier power-down control ---
#define GPIO_RF_AMP_PWDN         14  // pkg pin 13 -- active-high PWDN (confirmed): high disables the amp, low enables it.
                                     // No external pull resistor on this net, but the TRF37A73 has an INTERNAL
                                     // pulldown on PWDN (confirmed by the user against the amp IC's datasheet) --
                                     // so with no firmware driving this GPIO, PWDN floats low and the amp is
                                     // enabled by default at power-up, before any firmware runs.

// --- Handled by hardware or the SDK/TinyUSB implicitly -- no GPIO config
//     needed in application code. Listed for completeness against PINMAP.md:
//   XIN/XOUT        pkg pins 30/31   12MHz crystal (Y201) -- not a GPIO
//   SWCLK/SWDIO     pkg pins 33/34   SWD debug (J202) -- not a GPIO
//   RUN             pkg pin 35       reset -- not a GPIO
//   USB D-/D+       pkg pins 66/67   USB PHY (USB_DM/USB_DP) -- not a GPIO
//   3.3V/1.1V/GND   pkg pins 5,10,15,24,29,32,41,50,51,59,60,62,65,68,69,76,81

#endif
