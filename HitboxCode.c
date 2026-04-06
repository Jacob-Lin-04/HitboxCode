#include <stdbool.h>
#include <stdint.h>
#include "bsp/board_api.h"
#include "pico/stdlib.h"
#include "tusb.h"

#define SWITCH_GPIO 14
#define DEBOUNCE_MS 20

static bool raw_pressed = false;
static bool debounced_pressed = false;
static bool report_dirty = true;
static uint32_t last_bounce_ms = 0;
static uint32_t last_poll_ms = 0;

static void switch_init(void) {
    gpio_init(SWITCH_GPIO);
    gpio_set_dir(SWITCH_GPIO, GPIO_IN);
    gpio_pull_up(SWITCH_GPIO);
}

static bool switch_is_pressed(void) {
    return gpio_get(SWITCH_GPIO) == 0;
}

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

void tud_mount_cb(void) {
    report_dirty = true;
}

void tud_resume_cb(void) {
    report_dirty = true;
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type,
                               uint8_t *buffer, uint16_t reqlen) {
    (void) instance;
    (void) report_id;
    (void) report_type;
    (void) buffer;
    (void) reqlen;
    return 0;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type,
                           uint8_t const *buffer, uint16_t bufsize) {
    (void) instance;
    (void) report_id;
    (void) report_type;
    (void) buffer;
    (void) bufsize;
}

int main(void) {
    board_init();
    switch_init();
    tusb_init();

    while (true) {
        tud_task();
        hid_task();
    }
}
