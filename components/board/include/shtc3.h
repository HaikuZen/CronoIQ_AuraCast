// Sensirion SHTC3 temperature / humidity sensor (I2C 0x70).
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// i2c_bus_init() must have run first.
esp_err_t shtc3_init(void);

// Wake, measure (polling), sleep. Either out pointer may be NULL.
esp_err_t shtc3_read(float *temp_c, float *rh_pct);

#ifdef __cplusplus
}
#endif
