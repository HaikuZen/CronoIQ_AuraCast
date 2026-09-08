#include "display_st7305.h"

#include <string.h>
#include <assert.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

// Pin map — Waveshare ESP32-S3-RLCD-4.2
#define PIN_SCK   GPIO_NUM_11
#define PIN_MOSI  GPIO_NUM_12
#define PIN_CS    GPIO_NUM_40
#define PIN_DC    GPIO_NUM_5
#define PIN_RST   GPIO_NUM_41
#define LCD_HOST  SPI2_HOST
#define LCD_PCLK_HZ (10 * 1000 * 1000)

// Packed 1bpp buffer: 200 column-groups of 75 bytes (0x2A window 0x12..0x2A,
// 0x2B window 0x00..0xC7). Matches the Waveshare 09_LVGL_V9_Test packing.
#define FB_LEN ((RLCD_WIDTH * RLCD_HEIGHT) / 8)   // 15000

static const char *TAG = "rlcd";
static esp_lcd_panel_io_handle_t s_io;
static uint8_t *s_fb;

static void wr_cmd(uint8_t cmd)
{
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(s_io, cmd, NULL, 0));
}

static void wr_data(uint8_t d)
{
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(s_io, -1, &d, 1));
}

static void reset_panel(void)
{
    gpio_set_level(PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
    gpio_set_level(PIN_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
}

static void panel_init_seq(void)
{
    reset_panel();

    wr_cmd(0xD6); wr_data(0x17); wr_data(0x02);           // NVM load control
    wr_cmd(0xD1); wr_data(0x01);                          // booster enable
    wr_cmd(0xC0); wr_data(0x11); wr_data(0x04);           // gate voltage control

    wr_cmd(0xC1); wr_data(0x69); wr_data(0x69); wr_data(0x69); wr_data(0x69); // VSHP
    wr_cmd(0xC2); wr_data(0x19); wr_data(0x19); wr_data(0x19); wr_data(0x19); // VSLP
    wr_cmd(0xC4); wr_data(0x4B); wr_data(0x4B); wr_data(0x4B); wr_data(0x4B); // VSHN
    wr_cmd(0xC5); wr_data(0x19); wr_data(0x19); wr_data(0x19); wr_data(0x19); // VSLN

    wr_cmd(0xD8); wr_data(0x80); wr_data(0xE9);           // OSC settings
    wr_cmd(0xB2); wr_data(0x02);                          // frame rate

    wr_cmd(0xB3);                                         // update period gate (HPM)
    wr_data(0xE5); wr_data(0xF6); wr_data(0x05); wr_data(0x46); wr_data(0x77);
    wr_data(0x77); wr_data(0x77); wr_data(0x77); wr_data(0x76); wr_data(0x45);

    wr_cmd(0xB4);                                         // update period gate (LPM)
    wr_data(0x05); wr_data(0x46); wr_data(0x77); wr_data(0x77); wr_data(0x77);
    wr_data(0x77); wr_data(0x76); wr_data(0x45);

    wr_cmd(0x62); wr_data(0x32); wr_data(0x03); wr_data(0x1F);
    wr_cmd(0xB7); wr_data(0x13);
    wr_cmd(0xB0); wr_data(0x64);                          // multiplex ratio

    wr_cmd(0x11);                                         // sleep out
    vTaskDelay(pdMS_TO_TICKS(200));

    wr_cmd(0xC9); wr_data(0x00);
    wr_cmd(0x36); wr_data(0x48);                          // MADCTL
    wr_cmd(0x3A); wr_data(0x11);                          // data format
    wr_cmd(0xB9); wr_data(0x20);
    wr_cmd(0xB8); wr_data(0x29);
    wr_cmd(0x21);                                         // display inversion on

    wr_cmd(0x2A); wr_data(0x12); wr_data(0x2A);           // column address
    wr_cmd(0x2B); wr_data(0x00); wr_data(0xC7);           // row address

    wr_cmd(0x35); wr_data(0x00);                          // TE line on
    wr_cmd(0xD0); wr_data(0xFF);
    wr_cmd(0x38);                                         // HPM (high power / full refresh)
    wr_cmd(0x29);                                         // display on
}

void rlcd_init(void)
{
    spi_bus_config_t buscfg = {
        .mosi_io_num = PIN_MOSI,
        .miso_io_num = -1,
        .sclk_io_num = PIN_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = FB_LEN + 16,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_spi_config_t io_cfg = {
        .dc_gpio_num = PIN_DC,
        .cs_gpio_num = PIN_CS,
        .pclk_hz = LCD_PCLK_HZ,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 10,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_cfg, &s_io));

    gpio_config_t rst_cfg = {
        .pin_bit_mask = 1ULL << PIN_RST,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&rst_cfg));

    s_fb = heap_caps_malloc(FB_LEN, MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    assert(s_fb);

    panel_init_seq();
    rlcd_fill(true);
    rlcd_refresh();
    ESP_LOGI(TAG, "ST7305 panel ready (%dx%d)", RLCD_WIDTH, RLCD_HEIGHT);
}

void rlcd_fill(bool white)
{
    memset(s_fb, white ? 0xFF : 0x00, FB_LEN);
}

// Landscape mapping (see 09_LVGL_V9_Test RLCD_SetLandscapePixel).
// bit set = white, bit clear = black/ink.
void rlcd_pixel(uint16_t x, uint16_t y, bool white)
{
    if (x >= RLCD_WIDTH || y >= RLCD_HEIGHT) {
        return;
    }
    uint16_t inv_y = RLCD_HEIGHT - 1 - y;
    uint32_t index = (uint32_t)(x >> 1) * (RLCD_HEIGHT / 4) + (inv_y >> 2);
    uint8_t bit = 7 - (((inv_y & 0x03) << 1) | (x & 0x01));
    if (white) {
        s_fb[index] |= (1u << bit);
    } else {
        s_fb[index] &= ~(1u << bit);
    }
}

void rlcd_refresh(void)
{
    wr_cmd(0x2A); wr_data(0x12); wr_data(0x2A);
    wr_cmd(0x2B); wr_data(0x00); wr_data(0xC7);
    wr_cmd(0x2C);
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_color(s_io, -1, s_fb, FB_LEN));
}

void rlcd_flash(void)
{
    rlcd_fill(false);   // all black
    rlcd_refresh();
    vTaskDelay(pdMS_TO_TICKS(90));
    rlcd_fill(true);    // all white
    rlcd_refresh();
    vTaskDelay(pdMS_TO_TICKS(90));
}
