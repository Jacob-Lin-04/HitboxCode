#include <stdbool.h>
#include <stdint.h>
#include "bsp/board_api.h"
#include "pico/stdlib.h"
#include "tusb.h"

#define PIN_DPAD_UP 2
#define PIN_DPAD_DOWN 3
#define PIN_DPAD_LEFT 4
#define PIN_DPAD_RIGHT 5

#define PIN_SQUARE 6
#define PIN_CROSS 7
#define PIN_CIRCLE 8
#define PIN_TRIANGLE 9

#define PIN_L1 10
#define PIN_R1 11
#define PIN_L2 12
#define PIN_R2 13

#define PIN_CREATE 14
#define PIN_OPTIONS 15
#define PIN_L3 16
#define PIN_R3 17
#define PIN_PS 18

#define DEBOUNCE_MS 20

// Runtime input state:
// - raw_state tracks the most recent GPIO samples for all controller inputs.
// - debounced_state tracks the stable button state after debounce time.
// - report_dirty tells the main loop that the USB HID state changed and needs sending.
static uint32_t raw_state = 0;
static uint32_t debounced_state = 0;
static bool report_dirty = true;
static uint32_t last_bounce_ms = 0;
static uint32_t last_poll_ms = 0;
static hid_gamepad_report_t previous_report = { 0 };

// Bit positions for the packed button state used by the debounce and HID logic.
enum {
    BUTTON_DPAD_UP = 1u << 0,
    BUTTON_DPAD_DOWN = 1u << 1,
    BUTTON_DPAD_LEFT = 1u << 2,
    BUTTON_DPAD_RIGHT = 1u << 3,
    BUTTON_SQUARE = 1u << 4,
    BUTTON_CROSS = 1u << 5,
    BUTTON_CIRCLE = 1u << 6,
    BUTTON_TRIANGLE = 1u << 7,
    BUTTON_L1 = 1u << 8,
    BUTTON_R1 = 1u << 9,
    BUTTON_L2 = 1u << 10,
    BUTTON_R2 = 1u << 11,
    BUTTON_CREATE = 1u << 12,
    BUTTON_OPTIONS = 1u << 13,
    BUTTON_L3 = 1u << 14,
    BUTTON_R3 = 1u << 15,
    BUTTON_PS = 1u << 16
};

// Hardware setup for one active-low button input with an internal pull-up.
static void init_input_pin(uint pin) {
    gpio_init(pin);
    gpio_set_dir(pin, GPIO_IN);
    gpio_pull_up(pin);
}

// Initializes all controller input GPIOs used by the demo gamepad layout.
static void input_init(void) {
    init_input_pin(PIN_DPAD_UP);
    init_input_pin(PIN_DPAD_DOWN);
    init_input_pin(PIN_DPAD_LEFT);
    init_input_pin(PIN_DPAD_RIGHT);
    init_input_pin(PIN_SQUARE);
    init_input_pin(PIN_CROSS);
    init_input_pin(PIN_CIRCLE);
    init_input_pin(PIN_TRIANGLE);
    init_input_pin(PIN_L1);
    init_input_pin(PIN_R1);
    init_input_pin(PIN_L2);
    init_input_pin(PIN_R2);
    init_input_pin(PIN_CREATE);
    init_input_pin(PIN_OPTIONS);
    init_input_pin(PIN_L3);
    init_input_pin(PIN_R3);
    init_input_pin(PIN_PS);
}

// Reads one GPIO input and converts the active-low electrical level into a packed state bit.
static uint32_t read_button(uint pin, uint32_t mask) {
    return (gpio_get(pin) == 0) ? mask : 0;
}

// Reads all controller GPIO inputs and packs them into a compact state word.
static uint32_t read_input_state(void) {
    uint32_t state = 0;

    state |= read_button(PIN_DPAD_UP, BUTTON_DPAD_UP);
    state |= read_button(PIN_DPAD_DOWN, BUTTON_DPAD_DOWN);
    state |= read_button(PIN_DPAD_LEFT, BUTTON_DPAD_LEFT);
    state |= read_button(PIN_DPAD_RIGHT, BUTTON_DPAD_RIGHT);
    state |= read_button(PIN_SQUARE, BUTTON_SQUARE);
    state |= read_button(PIN_CROSS, BUTTON_CROSS);
    state |= read_button(PIN_CIRCLE, BUTTON_CIRCLE);
    state |= read_button(PIN_TRIANGLE, BUTTON_TRIANGLE);
    state |= read_button(PIN_L1, BUTTON_L1);
    state |= read_button(PIN_R1, BUTTON_R1);
    state |= read_button(PIN_L2, BUTTON_L2);
    state |= read_button(PIN_R2, BUTTON_R2);
    state |= read_button(PIN_CREATE, BUTTON_CREATE);
    state |= read_button(PIN_OPTIONS, BUTTON_OPTIONS);
    state |= read_button(PIN_L3, BUTTON_L3);
    state |= read_button(PIN_R3, BUTTON_R3);
    state |= read_button(PIN_PS, BUTTON_PS);

    return state;
}

// Converts 4 digital D-pad inputs into a single HID hat value.
static uint8_t make_hat(uint32_t state) {
    bool up = (state & BUTTON_DPAD_UP) != 0;
    bool down = (state & BUTTON_DPAD_DOWN) != 0;
    bool left = (state & BUTTON_DPAD_LEFT) != 0;
    bool right = (state & BUTTON_DPAD_RIGHT) != 0;

    if (up == down) {
        up = false;
        down = false;
    }
    if (left == right) {
        left = false;
        right = false;
    }

    if (up && right) {
        return GAMEPAD_HAT_UP_RIGHT;
    }
    if (right && down) {
        return GAMEPAD_HAT_DOWN_RIGHT;
    }
    if (down && left) {
        return GAMEPAD_HAT_DOWN_LEFT;
    }
    if (left && up) {
        return GAMEPAD_HAT_UP_LEFT;
    }
    if (up) {
        return GAMEPAD_HAT_UP;
    }
    if (right) {
        return GAMEPAD_HAT_RIGHT;
    }
    if (down) {
        return GAMEPAD_HAT_DOWN;
    }
    if (left) {
        return GAMEPAD_HAT_LEFT;
    }

    return GAMEPAD_HAT_CENTERED;
}

// Converts 2 digital directions into one analog stick axis value with neutral SOCD handling.
static int8_t make_axis(bool negative, bool positive) {
    if (negative == positive) {
        return 0;
    }

    return negative ? -127 : 127;
}

// Builds the generic HID gamepad report from the debounced GPIO state.
static hid_gamepad_report_t build_gamepad_report(uint32_t state) {
    hid_gamepad_report_t report = { 0 };

    report.x = 0;
    report.y = 0;
    report.z = 0;
    report.rz = 0;
    report.rx = 0;
    report.ry = 0;
    report.hat = make_hat(state);
    report.buttons = 0;

    if ((state & BUTTON_CROSS) != 0) {
        report.buttons |= GAMEPAD_BUTTON_SOUTH;
    }
    if ((state & BUTTON_CIRCLE) != 0) {
        report.buttons |= GAMEPAD_BUTTON_EAST;
    }
    if ((state & BUTTON_SQUARE) != 0) {
        report.buttons |= GAMEPAD_BUTTON_WEST;
    }
    if ((state & BUTTON_TRIANGLE) != 0) {
        report.buttons |= GAMEPAD_BUTTON_NORTH;
    }
    if ((state & BUTTON_L1) != 0) {
        report.buttons |= GAMEPAD_BUTTON_TL;
    }
    if ((state & BUTTON_R1) != 0) {
        report.buttons |= GAMEPAD_BUTTON_TR;
    }
    if ((state & BUTTON_L2) != 0) {
        report.buttons |= GAMEPAD_BUTTON_TL2;
    }
    if ((state & BUTTON_R2) != 0) {
        report.buttons |= GAMEPAD_BUTTON_TR2;
    }
    if ((state & BUTTON_CREATE) != 0) {
        report.buttons |= GAMEPAD_BUTTON_SELECT;
    }
    if ((state & BUTTON_OPTIONS) != 0) {
        report.buttons |= GAMEPAD_BUTTON_START;
    }
    if ((state & BUTTON_PS) != 0) {
        report.buttons |= GAMEPAD_BUTTON_MODE;
    }
    if ((state & BUTTON_L3) != 0) {
        report.buttons |= GAMEPAD_BUTTON_THUMBL;
    }
    if ((state & BUTTON_R3) != 0) {
        report.buttons |= GAMEPAD_BUTTON_THUMBR;
    }

    return report;
}

// Sends the current application state to the host as a USB HID gamepad report.
// This is the point where the debounced GPIO state becomes a real USB controller update.
static void send_gamepad_report(uint32_t state) {
    if (!tud_hid_ready()) {
        return;
    }

    hid_gamepad_report_t report = build_gamepad_report(state);

    if (memcmp(&report, &previous_report, sizeof(report)) != 0) {
        tud_hid_report(0, &report, sizeof(report));
        previous_report = report;
    }

    report_dirty = false;
}

// Main controller-style input task:
// - polls the controller GPIOs
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
        send_gamepad_report(debounced_state);
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
// This generic gamepad device does not provide control-transfer reports.
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type,
                               uint8_t *buffer, uint16_t reqlen) {
    (void) instance;
    (void) report_id;
    (void) report_type;
    (void) buffer;
    (void) reqlen;
    return 0;
}

// TinyUSB HID callback for SET_REPORT requests.
// This demo does not consume any host-to-device output reports.
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
