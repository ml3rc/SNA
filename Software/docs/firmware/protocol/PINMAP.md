# RP2350B (U202) pin map

Package-pin column extracted and resolved (through series/damping resistors) from `Altium/Project Outputs for SNA/SNA.NET`. GPIO column cross-checked against the official RP2350 datasheet (`Figure 3. RP2350 Pinout for QFN-80`, §1.2.1.2) rendered and read directly — not inferred. Regenerate the package-pin/signal columns from the netlist if the schematic changes; the package-pin→GPIO mapping is fixed silicon and never changes.

| Pin | GPIO | Signal | Function |
|---|---|---|---|
| 1 | GPIO4 | `RF_SW_CPLR_2.C2` | Coupler-bank switch 2, ctrl B |
| 2 | GPIO5 | `RF_SW_CPLR_2.C1` | Coupler-bank switch 2, ctrl A |
| 3 | GPIO6 | `RF_SW_CPLR_3.C2` | Coupler-bank switch 3, ctrl B |
| 4 | GPIO7 | `RF_SW_CPLR_3.C1` | Coupler-bank switch 3, ctrl A |
| 6 | GPIO8 | `RF_SW_CPLR_4.C2` | Coupler-bank switch 4, ctrl B |
| 7 | GPIO9 | `RF_SW_CPLR_4.C1` | Coupler-bank switch 4, ctrl A |
| 8 | GPIO10 | `RF_SW_CPLR_5.C2` | Coupler-bank switch 5, ctrl B |
| 9 | GPIO11 | `RF_SW_CPLR_5.C1` | Coupler-bank switch 5, ctrl A |
| 11 | GPIO12 | `RF_SW_CPLR_6.C2` | Coupler-bank switch 6, ctrl B |
| 12 | GPIO13 | `RF_SW_CPLR_6.C1` | Coupler-bank switch 6, ctrl A |
| 13 | GPIO14 | `RF_AMP_PWDN` | RF-Attenuation/Amp amplifier (TRF37A73) power-down control, active-high (pin high = amp disabled, low = enabled). Net name `NetC419_2` in the netlist extraction was just an auto-generated label, not the signal's real name — identity and polarity confirmed by the user directly from the schematic. No external pull resistor on this net, but the TRF37A73 has an internal pulldown on PWDN — so **the amp is enabled by default with no firmware running** (floats low = enabled), unlike the MAX2871's CE (which has an explicit external pull-down, see below, and defaults safely disabled). |
| 14 | GPIO15 | `RF_SW_PORT.C1` | Port-select switch, ctrl A |
| 16 | GPIO16 | `RF_SW_PORT.C2` | Port-select switch, ctrl B |
| 17 | GPIO17 | `RF_Attenuator.D6` | Digital attenuator, bit 6 (MSB) |
| 18 | GPIO18 | `RF_Attenuator.D5` | Digital attenuator, bit 5 |
| 19 | GPIO19 | `RF_Attenuator.D4` | Digital attenuator, bit 4 |
| 20 | GPIO20 | `RF_Attenuator.D3` | Digital attenuator, bit 3 |
| 21 | GPIO21 | `RF_Attenuator.D2` | Digital attenuator, bit 2 |
| 22 | GPIO22 | `RF_Attenuator.D1` | Digital attenuator, bit 1 |
| 23 | GPIO23 | `RF_Attenuator.D0` | Digital attenuator, bit 0 (LSB) |
| 25 | GPIO24 | `RF_Attenuator.LE` | Digital attenuator, latch enable |
| 26 | GPIO25 | `SDI_Test_Button` | On-board SDI self-test button (input) |
| 27 | GPIO26 | `SDI_Test_Pass` | On-board SDI self-test LED, green (output) |
| 28 | GPIO27 | `SDI_Test_Fail` | On-board SDI self-test LED, red (output) |
| 30 | XIN | `XIN` | 12 MHz crystal (dedicated pin, not GPIO) |
| 31 | XOUT | `XOUT` | 12 MHz crystal (dedicated pin, not GPIO) |
| 33 | SWCLK | `SWCLK` | Debug (J202) (dedicated pin, not GPIO) |
| 34 | SWDIO | `SWDIO` | Debug (J202) (dedicated pin, not GPIO) |
| 35 | RUN | `RUN` | Reset (dedicated pin, not GPIO) |
| 36 | GPIO28 | `L_Port1_Out_EN` | Status LED |
| 37 | GPIO29 | `L_Port_Z50` | Status LED (50Ω/75Ω indicator) |
| 38 | GPIO30 | `L_Ready` | Status LED |
| 39 | GPIO31 | `L_Busy` | Status LED |
| 40 | GPIO32 | `I2C0.SDA` | EEPROM (U203) |
| 42 | GPIO33 | `I2C0.SCL` | EEPROM (U203) |
| 43 | GPIO34 | `RF_SRC.LD` | MAX2871 lock detect (input) |
| 44 | GPIO35 | `RF_SRC.CE` | MAX2871 chip enable |
| 45 | GPIO36 | `RF_SRC.MUX` | MAX2871 MUX/readback |
| 46 | GPIO37 | `RF_SRC.LE` | MAX2871 SPI latch enable |
| 47 | GPIO38 | `RF_SRC.CLK` | MAX2871 SPI clock |
| 48 | GPIO39 | `RF_SRC.DATA` | MAX2871 SPI data |
| 49 | GPIO40 (ADC0) | `RF_SRC.RFOUT_EN` | MAX2871 RF output enable (ADC-capable pin, used digital) |
| 52 | GPIO41 (ADC1) | `RF_SW_LPF_1.C2` | LPF-bank switch 1, ctrl B (ADC-capable pin, used digital) |
| 53 | GPIO42 (ADC2) | `RF_SW_LPF_1.C1` | LPF-bank switch 1, ctrl A (ADC-capable pin, used digital) |
| 54 | GPIO43 (ADC3) | `RF_SW_LPF_2.C2` | LPF-bank switch 2, ctrl B (ADC-capable pin, used digital) |
| 55 | GPIO44 (ADC4) | `RF_SW_LPF_2.C1` | LPF-bank switch 2, ctrl A (ADC-capable pin, used digital) |
| 56 | **GPIO45 (ADC5)** | `RF_Detector.OUT` | Log detector output → **onboard ADC input, used as analog** (confirms this pin was deliberately chosen for its ADC capability) |
| 57 | GPIO46 (ADC6) | `RF_Detector.EN` | Log detector enable (ADC-capable pin, used digital) |
| 58 | GPIO47 (ADC7) | `L_Booted` | Status LED (ADC-capable pin, used digital) |
| 61 | VREG_AVDD | `NetC207_2` | Internal core regulator — power, not GPIO |
| 63 | VREG_LX | `NetL203_2` | Internal core regulator — power, not GPIO |
| 64 | VREG_VIN | `NetC205_2` | Internal core regulator — power, not GPIO |
| 66 | USB_DM | `USB_D_TER_N` | USB D− (dedicated pin, not GPIO) |
| 67 | USB_DP | `USB_D_TER_P` | USB D+ (dedicated pin, not GPIO) |
| 70 | QSPI_SD3 | `QSPI_SD3` | Boot flash (U201) (dedicated pin, not GPIO) |
| 71 | QSPI_SCLK | `QSPI_SCLK` | Boot flash (U201) (dedicated pin, not GPIO) |
| 72 | QSPI_SD0 | `QSPI_SD0` | Boot flash (U201) (dedicated pin, not GPIO) |
| 73 | QSPI_SD2 | `QSPI_SD2` | Boot flash (U201) (dedicated pin, not GPIO) |
| 74 | QSPI_SD1 | `QSPI_SD1` | Boot flash (U201) (dedicated pin, not GPIO) |
| 75 | QSPI_SS | `QSPI_SS` | Boot flash (U201) (dedicated pin, not GPIO) |
| 77 | GPIO0 | `RF_SW_Impedance.C2` | 50Ω/75Ω switch, ctrl B |
| 78 | GPIO1 | `RF_SW_Impedance.C1` | 50Ω/75Ω switch, ctrl A |
| 79 | GPIO2 | `RF_SW_CPLR_1.C2` | Coupler-bank switch 1, ctrl B |
| 80 | GPIO3 | `RF_SW_CPLR_1.C1` | Coupler-bank switch 1, ctrl A |

All 48 GPIOs (0–47) are accounted for exactly once above — matches the Dokumentation text's "48 GPIOs". Power/ground pins (5, 10, 15, 24, 29, 32, 41, 50, 51, 59, 60, 62, 65, 68, 69, 76, 81) omitted — 3.3V/1.1V/GND, not GPIO.

## Source

QFN-80 package-pin ↔ GPIO mapping is fixed silicon, read directly off the official datasheet figure (not a third-party or web-search summary, both of which were cross-checked and matched — see `rp2350-datasheet.pdf` §1.2.1.2, Figure 3, "RP2350 Pinout for QFN-80"):

```
Left edge   (pins 1–20):  GPIO4..GPIO11(w/IOVDD@5,DVDD@10), GPIO12..GPIO20(w/IOVDD@15)
Bottom edge (pins 21–40): GPIO21..GPIO27(w/IOVDD@24,29), XIN@30, XOUT@31, DVDD@32, SWCLK@33, SWDIO@34, RUN@35, GPIO28..GPIO32
Right edge  (pins 41–60): IOVDD@41, GPIO33..GPIO39, GPIO40_ADC0@49, IOVDD@50, DVDD@51, GPIO41_ADC1..GPIO47_ADC7, ADC_AVDD@59, IOVDD@60
Top edge    (pins 61–80): VREG_AVDD..VREG_FB(61-65), USB_DM@66, USB_DP@67, USB_OTP_VDD@68, QSPI_IOVDD@69, QSPI_SD3/SCLK/SD0/SD2/SD1/SS(70-75), IOVDD@76, GPIO0..GPIO3(77-80)
```

Status LEDs use inverted logic on some pins per the schematic notes (e.g. one polarity for 50Ω, the other for 75Ω on the same physical indicator) — verify active-high/low per LED against the schematic before wiring up firmware, this netlist extraction doesn't carry polarity.
