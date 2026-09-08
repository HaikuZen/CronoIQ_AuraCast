// Shared I2C master bus (port 0) for the SHTC3 sensor and PCF85063 RTC.
#pragma once

#include "esp_err.h"
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BOARD_I2C_SDA GPIO_NUM_13
#define BOARD_I2C_SCL GPIO_NUM_14

esp_err_t i2c_bus_init(void);
i2c_master_bus_handle_t i2c_bus_handle(void);

#ifdef __cplusplus
}
#endif
