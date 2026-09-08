// Runtime configuration: loaded from /sdcard/config.json, else compiled defaults.
#pragma once

#include <stdbool.h>

typedef struct {
    char wifi_ssid[33];
    char wifi_pass[65];
    char tz[48];            // POSIX TZ string
    char ntp_server[64];
    char provider[24];      // visualcrossing | openmeteo | openweathermap
    char weather_key[96];
    char location[80];      // "lat,lon" (all providers) or a place name (VC only)
    char city[48];          // optional display label; empty = auto place-name lookup
    bool units_metric;      // true: degC ; false: degF
    int  forecast_days;     // 1..5
    int  temp_offset_dc;    // SHTC3 correction, tenths of degC (e.g. -40 = -4.0 C)
    bool from_sd;           // true if any value came from the SD card
} app_config_t;

// Reads the config once. Safe to call before or after the SD card is mounted;
// if the card/file is absent or invalid, compiled defaults are used and the
// function still succeeds.
void app_config_load(void);

const app_config_t *app_config_get(void);
