#include <stdio.h>
#include <stdbool.h>
#include "pico/stdlib.h"

#define SWITCH_GPIO 14
#define DEBOUNCE_MS 20
#define USB_WAIT_TIMEOUT_MS 8000
#define HEARTBEAT_MS 1000

int main(void) {
    stdio_init_all();

    gpio_init(SWITCH_GPIO);
    gpio_set_dir(SWITCH_GPIO, GPIO_IN);
    gpio_pull_up(SWITCH_GPIO);

    // Wait a bit for the USB CDC serial monitor to connect so startup prints are visible.
    absolute_time_t usb_wait_deadline = make_timeout_time_ms(USB_WAIT_TIMEOUT_MS);
    while (!stdio_usb_connected() && absolute_time_diff_us(get_absolute_time(), usb_wait_deadline) > 0) {
        sleep_ms(10);
    }

    printf("Switch demo ready on GP%d.\n", SWITCH_GPIO);
    printf("Wire the switch between GP%d and GND.\n", SWITCH_GPIO);
    printf("Pressing the switch will print a message here.\n");
    printf("USB serial connected: %s\n", stdio_usb_connected() ? "yes" : "no");

    bool last_pressed = false;
    absolute_time_t next_heartbeat = make_timeout_time_ms(HEARTBEAT_MS);

    while (true) {
        bool pressed = gpio_get(SWITCH_GPIO) == 0;

        if (pressed && !last_pressed) {
            sleep_ms(DEBOUNCE_MS);

            if (gpio_get(SWITCH_GPIO) == 0) {
                printf("Switch pressed on GP%d\n", SWITCH_GPIO);
                last_pressed = true;
            }
        } else if (!pressed) {
            last_pressed = false;
        }

        if (absolute_time_diff_us(get_absolute_time(), next_heartbeat) <= 0) {
            printf("Waiting for switch on GP%d... current level=%d\n", SWITCH_GPIO, gpio_get(SWITCH_GPIO));
            next_heartbeat = make_timeout_time_ms(HEARTBEAT_MS);
        }

        sleep_ms(5);
    }
}
