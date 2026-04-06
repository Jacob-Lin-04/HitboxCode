#include <stdbool.h>
#include <stdint.h>
#include "bsp/board_api.h"
#include "pico/stdlib.h"
#include "tusb.h"

#define SWITCH_GPIO 14
#define DEBOUNCE_MS 20

// Runtime input state:
// - raw_pressed tracks the most recent GPIO sample.
// - debounced_pressed tracks the stable button state after debounce time.
// - report_dirty tells the main loop that the USB HID state changed and needs sending.
static bool raw_pressed = false;
static bool debounced_pressed = false;
static bool report_dirty = true;
static uint32_t last_bounce_ms = 0;
static uint32_t last_poll_ms = 0;

// Hardware setup for the one test switch. The internal pull-up keeps the input high
// until the button shorts GP14 to ground.
static void switch_init(void) {
    gpio_init(SWITCH_GPIO);
    gpio_set_dir(SWITCH_GPIO, GPIO_IN);
    gpio_pull_up(SWITCH_GPIO);
}

// Reads the electrical state of the switch and converts it into a logical "pressed"
// value for the rest of the program.
static bool switch_is_pressed(void) {
    return gpio_get(SWITCH_GPIO) == 0;
}

// Sends the current application state to the host as a USB HID keyboard report.
// This is the point where our local button state becomes a real USB keyboard event.
static void send_keyboard_report(bool pressed) {
    if (!tud_hid_ready()) {
        return;
    }

    if (pressed) {
        uint8_t keycodes[6] = { HID_KEY_W, 0, 0, 0, 0, 0 };
        tud_hid_keyboard_report(0, 0, keycodes);
    } else {
        tud_hid_keyboard_report(0, 0, NULL);
    }

    report_dirty = false;
}

// Main controller-style input task:
// - polls the switch
// - debounces transitions
// - requests remote wake if the PC suspended USB
// - pushes a fresh HID report only when the stable state changes
static void hid_task(void) {
    uint32_t now = board_millis();
    if ((now - last_poll_ms) < 1) {
        return;
    }
    last_poll_ms = now;

    bool sample = switch_is_pressed();
    if (sample != raw_pressed) {
        raw_pressed = sample;
        last_bounce_ms = now;
    }

    if ((now - last_bounce_ms) >= DEBOUNCE_MS && debounced_pressed != raw_pressed) {
        debounced_pressed = raw_pressed;
        report_dirty = true;

        if (tud_suspended() && debounced_pressed) {
            tud_remote_wakeup();
        }
    }

    if (report_dirty) {
        send_keyboard_report(debounced_pressed);
    }
}

// TinyUSB device callback: called after the host finishes enumerating the device.
// We mark the report dirty so the host receives the current button state promptly.
void tud_mount_cb(void) {
    report_dirty = true;
}

// TinyUSB device callback: called when USB resumes from suspend.
// We mark the report dirty for the same reason as mount.
void tud_resume_cb(void) {
    report_dirty = true;
}

// TinyUSB HID callback for GET_REPORT control requests.
// Many simple keyboards do not need to provide data here, so returning 0 is valid.
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type,
                               uint8_t *buffer, uint16_t reqlen) {
    (void) instance;
    (void) report_id;
    (void) report_type;
    (void) buffer;
    (void) reqlen;
    return 0;
}

// TinyUSB HID callback for SET_REPORT requests, typically used for host-to-device HID
// output such as keyboard LEDs. This demo does not consume any host output reports.
void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type,
                           uint8_t const *buffer, uint16_t bufsize) {
    (void) instance;
    (void) report_id;
    (void) report_type;
    (void) buffer;
    (void) bufsize;
}

// Firmware entry point:
// - initialize TinyUSB board support and GPIO
// - start the USB device stack
// - continuously service USB traffic and input scanning
int main(void) {
    board_init();
    switch_init();
    tusb_init();

    while (true) {
        tud_task();
        hid_task();
    }
}
