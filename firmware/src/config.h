/*
 * Controller Config
 * WHowe <github.com/whowechina>
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>
#include <stdbool.h>

typedef struct __attribute ((packed)) {
    uint8_t h; // hue;
    uint8_t s; // saturation;
    uint8_t v; // value;
} hsv_t;

typedef struct __attribute ((packed)) {
    hsv_t key_off[11];
    hsv_t key_on[11];
    struct {
        uint8_t start;
        uint8_t num;
        uint8_t effect;
        uint8_t param;
        uint8_t brightness;
        bool reversed;
    } tt_led;
    bool tt_sensor_reversed;
    struct {
        uint8_t play_vol;
        uint8_t filter;
        uint8_t eq_low;
        uint8_t eq_hi;
    } effects;
} iidx_cfg_t;

extern iidx_cfg_t *iidx_cfg;

void config_init();
void config_changed(); // Notify the config has changed

#endif
