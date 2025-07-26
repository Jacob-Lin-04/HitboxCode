#include "tusb.h"

static const uint8_t hid_report_descriptor[] = {
    // Expanded from TUD_HID_REPORT_DESC_KEYBOARD()
    0x05, 0x01, 0x09, 0x06, /* ... */ 0xC0
};

uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance) {
    (void)instance;
    return hid_report_descriptor;
}

uint16_t tud_hid_descriptor_report_size_cb(uint8_t instance) {
    (void)instance;
    return sizeof(hid_report_descriptor);
}
