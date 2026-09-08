#include "shtc3.h"
#include "i2c_bus.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_check.h"

static const char *TAG = "shtc3";

#define SHTC3_ADDR       0x70
#define CMD_WAKEUP       0x3517
#define CMD_SLEEP        0xB098
#define CMD_SOFT_RESET   0x805D
#define CMD_READ_ID      0xEFC8
#define CMD_MEAS_T_RH    0x7866   // T first, clock stretch disabled

static i2c_master_dev_handle_t s_dev;

static esp_err_t send_cmd(uint16_t cmd)
{
    uint8_t b[2] = { cmd >> 8, cmd & 0xFF };
    return i2c_master_transmit(s_dev, b, 2, 100);
}

static uint8_t crc8(const uint8_t *data, int len)
{
    uint8_t crc = 0xFF;
    for (int i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc & 0x80) ? (crc << 1) ^ 0x31 : (crc << 1);
        }
    }
    return crc;
}

esp_err_t shtc3_init(void)
{
    if (s_dev) {
        return ESP_OK;
    }
    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = SHTC3_ADDR,
        .scl_speed_hz = 400000,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(i2c_bus_handle(), &cfg, &s_dev), TAG, "add dev");

    send_cmd(CMD_WAKEUP);
    vTaskDelay(pdMS_TO_TICKS(2));
    send_cmd(CMD_SOFT_RESET);
    vTaskDelay(pdMS_TO_TICKS(2));

    uint8_t idcmd[2] = { CMD_READ_ID >> 8, CMD_READ_ID & 0xFF };
    uint8_t rx[3] = { 0 };
    esp_err_t err = i2c_master_transmit_receive(s_dev, idcmd, 2, rx, 3, 100);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "id=0x%04x", (rx[0] << 8) | rx[1]);
    } else {
        ESP_LOGW(TAG, "id read failed: %s", esp_err_to_name(err));
    }
    send_cmd(CMD_SLEEP);
    return err;
}

esp_err_t shtc3_read(float *temp_c, float *rh_pct)
{
    if (!s_dev) {
        return ESP_ERR_INVALID_STATE;
    }
    send_cmd(CMD_WAKEUP);
    vTaskDelay(pdMS_TO_TICKS(2));

    esp_err_t err = send_cmd(CMD_MEAS_T_RH);
    if (err != ESP_OK) {
        send_cmd(CMD_SLEEP);
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(20));

    uint8_t d[6] = { 0 };
    err = i2c_master_receive(s_dev, d, 6, 100);
    send_cmd(CMD_SLEEP);
    if (err != ESP_OK) {
        return err;
    }
    if (crc8(&d[0], 2) != d[2] || crc8(&d[3], 2) != d[5]) {
        return ESP_ERR_INVALID_CRC;
    }

    uint16_t raw_t = (d[0] << 8) | d[1];
    uint16_t raw_h = (d[3] << 8) | d[4];
    if (temp_c) {
        *temp_c = -45.0f + 175.0f * (float)raw_t / 65536.0f;
    }
    if (rh_pct) {
        *rh_pct = 100.0f * (float)raw_h / 65536.0f;
    }
    return ESP_OK;
}
