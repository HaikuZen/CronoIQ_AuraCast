#include "i2c_bus.h"
#include "esp_log.h"

static const char *TAG = "i2c_bus";
static i2c_master_bus_handle_t s_bus;

esp_err_t i2c_bus_init(void)
{
    if (s_bus) {
        return ESP_OK;
    }
    i2c_master_bus_config_t cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_NUM_0,
        .scl_io_num = BOARD_I2C_SCL,
        .sda_io_num = BOARD_I2C_SDA,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t err = i2c_new_master_bus(&cfg, &s_bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_new_master_bus: %s", esp_err_to_name(err));
    }
    return err;
}

i2c_master_bus_handle_t i2c_bus_handle(void)
{
    return s_bus;
}
