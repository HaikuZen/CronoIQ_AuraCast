// NXP PCF85063A RTC (I2C 0x51).
#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int year;    // full year, e.g. 2026
    int month;   // 1..12
    int day;     // 1..31
    int hour;    // 0..23
    int minute;  // 0..59
    int second;  // 0..59
    int weekday; // 0..6, 0 = Sunday
} rtc_time_t;

// i2c_bus_init() must have run first. Clears the STOP bit and selects 24h mode.
esp_err_t pcf85063_init(void);

esp_err_t pcf85063_get(rtc_time_t *t);
esp_err_t pcf85063_set(const rtc_time_t *t);

// True if the oscillator-stop flag is set (time is not trustworthy, e.g. first
// power-up with no backup cell).
bool pcf85063_time_invalid(void);

#ifdef __cplusplus
}
#endif
