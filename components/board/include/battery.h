// Battery monitor: GPIO4 = ADC1_CH3 behind a divider (x3).
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t battery_init(void);

// Pack voltage in volts, or 0 on error.
float battery_voltage(void);

// Rough state of charge, 0..100.
int battery_percent(void);

#ifdef __cplusplus
}
#endif
