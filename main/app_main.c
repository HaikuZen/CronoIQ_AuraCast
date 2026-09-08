#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_heap_caps.h"

#include "board.h"
#include "app_config.h"
#include "app_sensors.h"
#include "app_weather.h"
#include "time_sync.h"
#include "wifi_sta.h"
#include "ui/ui.h"

static const char *TAG = "app";

static void on_got_ip(void)
{
    time_sync_start_sntp();
}

static void on_button(board_btn_t btn, board_btn_evt_t evt, void *ctx)
{
    (void)ctx;
    if (btn == BOARD_BTN_KEY && evt == BOARD_BTN_EVT_SINGLE) {
        ui_next_page();
    } else if (btn == BOARD_BTN_BOOT && evt == BOARD_BTN_EVT_LONG) {
        ESP_LOGI(TAG, "BOOT long-press: forcing weather refresh");
        app_weather_request_refresh();
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "CronoIQ_AuraCast boot");

    // Display first so something is on screen ASAP.
    rlcd_init();
    lvgl_port_init();

    // Sensors / RTC on the shared I2C bus.
    ESP_ERROR_CHECK(i2c_bus_init());
    shtc3_init();
    pcf85063_init();
    battery_init();

    // Configuration (mounts the SD card; falls back to compiled defaults).
    app_config_load();
    const app_config_t *cfg = app_config_get();

    // Clock: seed from RTC now, NTP later.
    time_sync_init(cfg->tz, cfg->ntp_server);

    // Background producers.
    app_sensors_start(cfg->temp_offset_dc);
    app_weather_start();

    // UI + input.
    ui_start();
    board_button_init();
    board_button_set_callback(on_button, NULL);

    // Network last.
    wifi_sta_start(cfg->wifi_ssid, cfg->wifi_pass, on_got_ip);

    ESP_LOGI(TAG, "init done; free heap %u, free psram %u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}
