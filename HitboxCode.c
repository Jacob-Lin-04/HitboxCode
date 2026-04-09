#include <stdbool.h>
#include <stdint.h>
#include "bsp/board_api.h"
#include "pico/stdlib.h"
#include "tusb.h"

#define PIN_DPAD_UP 2
#define PIN_DPAD_DOWN 3
#define PIN_DPAD_LEFT 4
#define PIN_DPAD_RIGHT 5

#define PIN_X 6
#define PIN_A 7
#define PIN_B 8
#define PIN_Y 9

#define PIN_LB 10
#define PIN_RB 11
#define PIN_LT 12
#define PIN_RT 13

#define PIN_BACK 14
#define PIN_START 15
#define PIN_L3 16
#define PIN_R3 17
#define PIN_XBOX 18

#define DEBOUNCE_MS 20

/*
Reference table for the current generic HID gamepad mapping.

GPIO     Control   HID meaning            Typical Windows joy.cpl indicator
GP2      Up        D-pad up               Point of View Hat Up
GP3      Down      D-pad down             Point of View Hat Down
GP4      Left      D-pad left             Point of View Hat Left
GP5      Right     D-pad right            Point of View Hat Right
GP6      X         GAMEPAD_BUTTON_WEST    Button 5
GP7      A         GAMEPAD_BUTTON_SOUTH   Button 1
GP8      B         GAMEPAD_BUTTON_EAST    Button 2
GP9      Y         GAMEPAD_BUTTON_NORTH   Button 4
GP10     LB        GAMEPAD_BUTTON_TL      Button 7
GP11     RB        GAMEPAD_BUTTON_TR      Button 8
GP12     LT        GAMEPAD_BUTTON_TL2     Button 9 and X Rotation axis
GP13     RT        GAMEPAD_BUTTON_TR2     Button 10 and Y Rotation axis
GP14     Back      GAMEPAD_BUTTON_SELECT  Button 11
GP15     Start     GAMEPAD_BUTTON_START   Button 12
GP16     L3        GAMEPAD_BUTTON_THUMBL  Button 14
GP17     R3        GAMEPAD_BUTTON_THUMBR  Button 15
GP18     Xbox      GAMEPAD_BUTTON_MODE    Button 13

Notes:
- Windows joy.cpl numbers buttons from 1, while the HID bit fields are zero-based.
- LT and RT are also exposed as analog trigger axes in this firmware, so joy.cpl may
  show them both as buttons and as X/Y rotation movement.
*/

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
    BUTTON_X = 1u << 4,
    BUTTON_A = 1u << 5,
    BUTTON_B = 1u << 6,
    BUTTON_Y = 1u << 7,
    BUTTON_LB = 1u << 8,
    BUTTON_RB = 1u << 9,
    BUTTON_LT = 1u << 10,
    BUTTON_RT = 1u << 11,
    BUTTON_BACK = 1u << 12,
    BUTTON_START = 1u << 13,
    BUTTON_L3 = 1u << 14,
    BUTTON_R3 = 1u << 15,
    BUTTON_XBOX = 1u << 16
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
    init_input_pin(PIN_X);
    init_input_pin(PIN_A);
    init_input_pin(PIN_B);
    init_input_pin(PIN_Y);
    init_input_pin(PIN_LB);
    init_input_pin(PIN_RB);
    init_input_pin(PIN_LT);
    init_input_pin(PIN_RT);
    init_input_pin(PIN_BACK);
    init_input_pin(PIN_START);
    init_input_pin(PIN_L3);
    init_input_pin(PIN_R3);
    init_input_pin(PIN_XBOX);
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
    state |= read_button(PIN_X, BUTTON_X);
    state |= read_button(PIN_A, BUTTON_A);
    state |= read_button(PIN_B, BUTTON_B);
    state |= read_button(PIN_Y, BUTTON_Y);
    state |= read_button(PIN_LB, BUTTON_LB);
    state |= read_button(PIN_RB, BUTTON_RB);
    state |= read_button(PIN_LT, BUTTON_LT);
    state |= read_button(PIN_RT, BUTTON_RT);
    state |= read_button(PIN_BACK, BUTTON_BACK);
    state |= read_button(PIN_START, BUTTON_START);
    state |= read_button(PIN_L3, BUTTON_L3);
    state |= read_button(PIN_R3, BUTTON_R3);
    state |= read_button(PIN_XBOX, BUTTON_XBOX);

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
// The logical mapping here follows Xbox-style semantics:
// - A/B/X/Y stay as A/B/X/Y
// - LB/RB stay as LB/RB
// - LT/RT drive the trigger axes
// - Back/Start/Xbox stay as Back/Start/Xbox
static hid_gamepad_report_t build_gamepad_report(uint32_t state) {
    hid_gamepad_report_t report = { 0 };

    report.x = 0;
    report.y = 0;
    report.z = 0;
    report.rz = 0;
    report.rx = (state & BUTTON_LT) != 0 ? 127 : 0;
    report.ry = (state & BUTTON_RT) != 0 ? 127 : 0;
    report.hat = make_hat(state);
    report.buttons = 0;

    if ((state & BUTTON_A) != 0) {
        report.buttons |= GAMEPAD_BUTTON_SOUTH;
    }
    if ((state & BUTTON_B) != 0) {
        report.buttons |= GAMEPAD_BUTTON_EAST;
    }
    if ((state & BUTTON_X) != 0) {
        report.buttons |= GAMEPAD_BUTTON_WEST;
    }
    if ((state & BUTTON_Y) != 0) {
        report.buttons |= GAMEPAD_BUTTON_NORTH;
    }
    if ((state & BUTTON_LB) != 0) {
        report.buttons |= GAMEPAD_BUTTON_TL;
    }
    if ((state & BUTTON_RB) != 0) {
        report.buttons |= GAMEPAD_BUTTON_TR;
    }
    if ((state & BUTTON_LT) != 0) {
        report.buttons |= GAMEPAD_BUTTON_TL2;
    }
    if ((state & BUTTON_RT) != 0) {
        report.buttons |= GAMEPAD_BUTTON_TR2;
    }
    if ((state & BUTTON_BACK) != 0) {
        report.buttons |= GAMEPAD_BUTTON_SELECT;
    }
    if ((state & BUTTON_START) != 0) {
        report.buttons |= GAMEPAD_BUTTON_START;
    }
    if ((state & BUTTON_XBOX) != 0) {
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

    uint32_t sample = read_input_state();
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
