#ifndef _TUSB_CONFIG_H_
#define _TUSB_CONFIG_H_

#ifdef __cplusplus
extern "C" {
#endif

// TinyUSB's rp2040 device-controller driver (dcd_rp2040.c) covers the RP2350's
// USB controller too (same IP block) -- confirmed correct by an actual build
// against pico-sdk 2.2.0 / its bundled TinyUSB: the SDK's own CMake glue
// selects this same "rp2040" TinyUSB port for RP2350 builds (see the
// "TinyUSB available at .../lib/tinyusb/hw/bsp/rp2040" configure-time
// message), not a dedicated RP2350 port.
#define CFG_TUSB_MCU OPT_MCU_RP2040
#define CFG_TUSB_OS OPT_OS_PICO
#define CFG_TUSB_RHPORT0_MODE (OPT_MODE_DEVICE | OPT_MODE_FULL_SPEED)

#ifndef CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_SECTION
#endif

#ifndef CFG_TUSB_MEM_ALIGN
#define CFG_TUSB_MEM_ALIGN __attribute__((aligned(4)))
#endif

#define CFG_TUD_ENDPOINT0_SIZE 64

// --- CDC (the only class this device implements) ---
#define CFG_TUD_CDC 1
#define CFG_TUD_CDC_RX_BUFSIZE 512
#define CFG_TUD_CDC_TX_BUFSIZE 512
#define CFG_TUD_CDC_EP_BUFSIZE 64

#define CFG_TUD_MSC 0
#define CFG_TUD_HID 0
#define CFG_TUD_MIDI 0
#define CFG_TUD_VENDOR 0

#ifdef __cplusplus
}
#endif

#endif
