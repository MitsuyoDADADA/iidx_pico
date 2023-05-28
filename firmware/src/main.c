/*
 * Controller Main
 * WHowe <github.com/whowechina>
 */

#include <stdint.h>
#include <stdbool.h>

#include "bsp/board.h"
#include "pico/multicore.h"
#include "pico/bootrom.h"
#include "pico/stdio.h"
#include "hardware/watchdog.h"

#include "btstack_event.h"
#include "pico/cyw43_arch.h"

#include "tusb.h"
#include "usb_descriptors.h"

#include "setup.h"

#include "buttons.h"
#include "rgb.h"
#include "turntable.h"

#include "tt_blade.h"
#include "tt_rainbow.h"

#include "config.h"
#include "save.h"

/* Measure the time of a function call */
#define RUN_TIME(func) \
   { uint64_t _t = time_us_64(); func; \
     printf(#func ":%lld\n", time_us_64() - _t); }

struct {
    uint16_t buttons;
    uint8_t joy[6];
} hid_report;

void report_usb_hid()
{
    if (tud_hid_ready()) {
        hid_report.joy[2] = iidx_cfg->effects.e1;
        hid_report.joy[3] = iidx_cfg->effects.e2;
        hid_report.joy[4] = iidx_cfg->effects.e3;
        hid_report.joy[5] = iidx_cfg->effects.e4;
        tud_hid_n_report(0x00, REPORT_ID_JOYSTICK, &hid_report, sizeof(hid_report));
    }
}

void boot_check()
{
    uint16_t key1 = (1 << (button_num() - 1));
    uint16_t key2 = (1 << (button_num() - 2));
    uint16_t buttons = button_read();
    if (!watchdog_caused_reboot() && (buttons & key1) && (buttons & key2)) {
        reset_usb_boot(0, 2);
    }
}
 
void mode_check()
{
    uint16_t key1 = (1 << (button_num() - 1));
    uint16_t key2 = (1 << (button_num() - 2));
    uint16_t buttons = button_read();
    if (buttons & key1) {
        iidx_cfg->konami = true;
        save_request(false);
    } else if (buttons & key2) {
        iidx_cfg->konami = false;
        save_request(false);
    }

    if (iidx_cfg->konami) {
        konami_mode();
    }
}

static bool request_core1_pause = false;

static void pause_core1(bool pause)
{
    request_core1_pause = pause;
    if (pause) {
        sleep_ms(5); /* wait for any IO ops to finish */
    }
}

static void core1_loop()
{
#define RUN_EVERY_N_MS(a, ms) { if (frame % ms == 0) a; }
    uint32_t frame = 0;
    while (true) {
        uint32_t angle = turntable_raw();
        rgb_set_angle(angle);

        uint8_t angle8 = turntable_read();
        hid_report.joy[0] = angle8;
        hid_report.joy[1] = 255 - angle8;

        RUN_EVERY_N_MS(rgb_update(), 2);
        turntable_update();
        frame++;
        do {
            sleep_ms(1);
        } while (request_core1_pause);
    }
}

static void boot_usb_check(uint16_t buttons)
{
    uint16_t usb_boot_keys = 0x1855; /* YES, NO, 1, 3, 5, 7 */
    if (buttons == usb_boot_keys) {
        reset_usb_boot(0, 2); // usb boot to flash
    }

    uint16_t factory_default_keys = 0x182a; /* YES, NO, 2, 4, 6 */
    if (buttons == factory_default_keys) {
        config_factory_reset();
        watchdog_enable(1, 1);
        while(1); // just reboot
    }
}

static void core0_loop()
{
    while (true)
    {
        tud_task();
        uint16_t buttons = button_read();
        boot_usb_check(buttons);
        uint16_t angle = turntable_raw() >> 4;
        if (setup_run(buttons, angle)) {
            rgb_force_display(setup_led_button, setup_led_tt);
        } else {
            hid_report.buttons = buttons;
            rgb_set_button_light(buttons);
            save_loop();
        }
        report_usb_hid();
    }
}


static btstack_packet_callback_registration_t hci_event_callback_registration;

static void packet_handler (uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size){
    UNUSED(size);
    UNUSED(channel);
    bd_addr_t local_addr;
    if (packet_type != HCI_EVENT_PACKET) return;
    switch(hci_event_packet_get_type(packet)){
        case BTSTACK_EVENT_STATE:
            if (btstack_event_state_get_state(packet) != HCI_STATE_WORKING) return;
            gap_local_bd_addr(local_addr);
            printf("BTstack up and running on %s.\n", bd_addr_to_str(local_addr));
            break;
        default:
            break;
    }
}

int init_bluetooth()
{
    // initialize CYW43 driver architecture (will enable BT if/because CYW43_ENABLE_BLUETOOTH == 1)
    if (cyw43_arch_init()) {
        printf("failed to initialise cyw43_arch\n");
        return -1;
    }

    // inform about BTstack state
    hci_event_callback_registration.callback = &packet_handler;
    hci_add_event_handler(&hci_event_callback_registration);

    return 0;
}

void init()
{
    board_init();
    tusb_init();

    init_bluetooth();
    button_init();
    tt_rainbow_init();
    tt_blade_init();
    rgb_init();
    turntable_init();

    boot_check();
    stdio_init_all();

    setup_init();
    config_init();
    save_init(pause_core1);

    mode_check();
}

int main(void)
{
    init();
    multicore_launch_core1(core1_loop);

    core0_loop();

    return 0;
}

// Invoked when received GET_REPORT control request
// Application must fill buffer report's content and return its length.
// Return zero will cause the stack to STALL request
uint16_t tud_hid_get_report_cb(uint8_t itf, uint8_t report_id,
                               hid_report_type_t report_type, uint8_t *buffer,
                               uint16_t reqlen)
{
    return 0;
}

// Invoked when received SET_REPORT control request or
// received data on OUT endpoint ( Report ID = 0, Type = 0 )
void tud_hid_set_report_cb(uint8_t itf, uint8_t report_id,
                           hid_report_type_t report_type, uint8_t const *buffer,
                           uint16_t bufsize)
{
    if ((report_id == REPORT_ID_LIGHTS) &&
        (report_type == HID_REPORT_TYPE_OUTPUT)) {
        if (bufsize >= rgb_button_num()) {
           rgb_set_hid_light(buffer, rgb_button_num());
        }
    }
}
