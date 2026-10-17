// USB device descriptors: a single CDC-ACM (virtual serial) interface, no
// other classes. Standard TinyUSB CDC-only descriptor layout.

#include <string.h>
#include "tusb.h"
#include "pico/unique_id.h"

// TinyUSB's own example VID:PID, fine for bring-up/development. Replace with
// a real allocated VID:PID pair (or a proper USB-IF PID under a shared VID)
// before this is ever distributed outside the lab bench.
#define USB_VID   0xCafe
#define USB_PID   0x4001
#define USB_BCD   0x0200

tusb_desc_device_t const desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = USB_BCD,
    .bDeviceClass       = TUSB_CLASS_MISC,
    .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol    = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = USB_VID,
    .idProduct          = USB_PID,
    .bcdDevice          = 0x0100,
    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,
    .bNumConfigurations = 0x01,
};

uint8_t const *tud_descriptor_device_cb(void) {
    return (uint8_t const *) &desc_device;
}

// String descriptor indices (string_desc_arr below).
enum {
    STRID_LANGID = 0,
    STRID_MANUFACTURER,
    STRID_PRODUCT,
    STRID_SERIAL,
    STRID_CDC,
    STRID_COUNT,
};

enum {
    ITF_NUM_CDC = 0,
    ITF_NUM_CDC_DATA,
    ITF_NUM_TOTAL,
};

#define EPNUM_CDC_NOTIF   0x81
#define EPNUM_CDC_OUT     0x02
#define EPNUM_CDC_IN      0x82

#define CONFIG_TOTAL_LEN  (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN)

uint8_t const desc_fs_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 100),
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, STRID_CDC, EPNUM_CDC_NOTIF, 8, EPNUM_CDC_OUT, EPNUM_CDC_IN, 64),
};

uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
    (void) index;
    return desc_fs_configuration;
}

static char const *string_desc_arr[] = {
    [STRID_LANGID] = NULL,                          // filled specially (language id)
    [STRID_MANUFACTURER] = "ml3rc",
    [STRID_PRODUCT] = "SNA Scalar Network Analyzer",
    [STRID_SERIAL] = NULL,                          // filled at runtime from the flash unique id
    [STRID_CDC] = "SNA Control",                    // CDC interface name
};
_Static_assert(sizeof(string_desc_arr) / sizeof(string_desc_arr[0]) == STRID_COUNT, "a string per index");

static uint16_t _desc_str[32];

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void) langid;
    size_t chr_count;

    if (index == STRID_LANGID) {
        _desc_str[1] = 0x0409; // English (US)
        chr_count = 1;
    } else {
        char serial_buf[PICO_UNIQUE_BOARD_ID_SIZE_BYTES * 2 + 1];
        const char *str;

        if (index == STRID_SERIAL) {
            pico_get_unique_board_id_string(serial_buf, sizeof(serial_buf));
            str = serial_buf;
        } else {
            if (index >= sizeof(string_desc_arr) / sizeof(string_desc_arr[0])) return NULL;
            str = string_desc_arr[index];
            if (!str) return NULL;
        }

        chr_count = strlen(str);
        if (chr_count > 31) chr_count = 31;
        for (size_t i = 0; i < chr_count; i++) {
            _desc_str[1 + i] = (uint16_t) str[i];
        }
    }

    _desc_str[0] = (uint16_t) ((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));

    return _desc_str;
}
