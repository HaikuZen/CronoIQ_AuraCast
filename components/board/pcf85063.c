#include "pcf85063.h"
#include "i2c_bus.h"

#include "esp_log.h"

static const char *TAG = "pcf85063";

#define PCF_ADDR      0x51
#define REG_CTRL1     0x00
#define REG_SECONDS   0x04   // bit7 = OS (oscillator stop)

static i2c_master_dev_handle_t s_dev;
static bool s_invalid;

static uint8_t bin2bcd(int v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }
static int bcd2bin(uint8_t v) { return ((v >> 4) & 0x0F) * 10 + (v & 0x0F); }

static esp_err_t rd(uint8_t reg, uint8_t *buf, size_t len)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, buf, len, 100);
}

static esp_err_t wr(uint8_t reg, const uint8_t *buf, size_t len)
{
    uint8_t tmp[8];
    if (len + 1 > sizeof(tmp)) {
        return ESP_ERR_INVALID_SIZE;
    }
    tmp[0] = reg;
    for (size_t i = 0; i < len; i++) {
        tmp[i + 1] = buf[i];
    }
    return i2c_master_transmit(s_dev, tmp, len + 1, 100);
}

esp_err_t pcf85063_init(void)
{
    if (s_dev) {
        return ESP_OK;
    }
    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = PCF_ADDR,
        .scl_speed_hz = 300000,
    };
    esp_err_t err = i2c_master_bus_add_device(i2c_bus_handle(), &cfg, &s_dev);
    if (err != ESP_OK) {
        return err;
    }

    // Control_1 = 0x00: normal mode, RTC running (STOP=0), 24h, no correction irq.
    uint8_t ctrl1 = 0x00;
    err = wr(REG_CTRL1, &ctrl1, 1);

    uint8_t sec = 0;
    if (rd(REG_SECONDS, &sec, 1) == ESP_OK) {
        s_invalid = (sec & 0x80) != 0;
    }
    ESP_LOGI(TAG, "init%s", s_invalid ? " (time invalid — OS flag set)" : "");
    return err;
}

bool pcf85063_time_invalid(void)
{
    return s_invalid;
}

esp_err_t pcf85063_get(rtc_time_t *t)
{
    if (!s_dev || !t) {
        return ESP_ERR_INVALID_STATE;
    }
    uint8_t d[7] = { 0 };
    esp_err_t err = rd(REG_SECONDS, d, 7);
    if (err != ESP_OK) {
        return err;
    }
    s_invalid = (d[0] & 0x80) != 0;
    t->second  = bcd2bin(d[0] & 0x7F);
    t->minute  = bcd2bin(d[1] & 0x7F);
    t->hour    = bcd2bin(d[2] & 0x3F);
    t->day     = bcd2bin(d[3] & 0x3F);
    t->weekday = bcd2bin(d[4] & 0x07);
    t->month   = bcd2bin(d[5] & 0x1F);
    t->year    = 2000 + bcd2bin(d[6]);
    return ESP_OK;
}

esp_err_t pcf85063_set(const rtc_time_t *t)
{
    if (!s_dev || !t) {
        return ESP_ERR_INVALID_STATE;
    }
    int yy = t->year - 2000;
    if (yy < 0 || yy > 99) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t d[7] = {
        bin2bcd(t->second) & 0x7F,   // also clears the OS flag
        bin2bcd(t->minute),
        bin2bcd(t->hour),
        bin2bcd(t->day),
        bin2bcd(t->weekday) & 0x07,
        bin2bcd(t->month),
        bin2bcd(yy),
    };
    esp_err_t err = wr(REG_SECONDS, d, 7);
    if (err == ESP_OK) {
        s_invalid = false;
    }
    return err;
}
