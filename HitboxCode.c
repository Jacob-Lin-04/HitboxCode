#include <stdbool.h>
#include <stdint.h>
#include "bsp/board_api.h"
#include "pico/stdlib.h"
#include "tusb.h"

#define PIN_W 12
#define PIN_A 13
#define PIN_S 14
#define PIN_D 15
#define DEBOUNCE_MS 20

// Runtime input state:
// - raw_state tracks the most recent GPIO samples for all movement buttons.
// - debounced_state tracks the stable button state after debounce time.
// - report_dirty tells the main loop that the USB HID state changed and needs sending.
static uint8_t raw_state = 0;
static uint8_t debounced_state = 0;
static bool report_dirty = true;
static uint32_t last_bounce_ms = 0;
static uint32_t last_poll_ms = 0;

// Bit positions for the packed button state used by the debounce and HID logic.
enum {
    BUTTON_W = 1u << 0,
    BUTTON_A = 1u << 1,
    BUTTON_S = 1u << 2,
    BUTTON_D = 1u << 3
};

// Hardware setup for one active-low button input with an internal pull-up.
static void init_input_pin(uint pin) {
    gpio_init(pin);
    gpio_set_dir(pin, GPIO_IN);
    gpio_pull_up(pin);
}

// Initializes all four movement inputs.
static void input_init(void) {
    init_input_pin(PIN_W);
    init_input_pin(PIN_A);
    init_input_pin(PIN_S);
    init_input_pin(PIN_D);
}

// Reads all four GPIO inputs and packs them into a compact state byte.
static uint8_t read_input_state(void) {
    uint8_t state = 0;

    if (gpio_get(PIN_W) == 0) {
        state |= BUTTON_W;
    }
    if (gpio_get(PIN_A) == 0) {
        state |= BUTTON_A;
    }
    if (gpio_get(PIN_S) == 0) {
        state |= BUTTON_S;
    }
    if (gpio_get(PIN_D) == 0) {
        state |= BUTTON_D;
    }

    return state;
}

// Sends the current application state to the host as a USB HID keyboard report.
// This is the point where the debounced GPIO state becomes a real USB keyboard event.
static void send_keyboard_report(uint8_t state) {
    if (!tud_hid_ready()) {
        return;
    }

    uint8_t keycodes[6] = { 0, 0, 0, 0, 0, 0 };
    uint8_t key_index = 0;

    if ((state & BUTTON_W) != 0) {
        keycodes[key_index++] = HID_KEY_W;
    }
    if ((state & BUTTON_A) != 0) {
        keycodes[key_index++] = HID_KEY_A;
    }
    if ((state & BUTTON_S) != 0) {
        keycodes[key_index++] = HID_KEY_S;
    }
    if ((state & BUTTON_D) != 0) {
        keycodes[key_index++] = HID_KEY_D;
    }

    tud_hid_keyboard_report(0, 0, keycodes);

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

    uint8_t sample = read_input_state();
    if (sample != raw_state) {
        raw_state = sample;
        last_bounce_ms = now;
    }

    if ((now - last_bounce_ms) >= DEBOUNCE_MS && debounced_state != raw_state) {
        debounced_state = raw_state;
        report_dirty = true;

        if (tud_suspended() && debounced_state != 0) {
            tud_remote_wakeup();
        }
    }

    if (report_dirty) {
        send_keyboard_report(debounced_state);
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
    input_init();
    tusb_init();

    while (true) {
        tud_task();
        hid_task();
    }
}
