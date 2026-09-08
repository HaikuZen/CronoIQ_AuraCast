#include "app_weather.h"
#include "app_config.h"
#include "time_sync.h"
#include "wifi_sta.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"

static const char *TAG = "app_weather";

#define REFRESH_PERIOD_MS (30 * 60 * 1000)

static SemaphoreHandle_t s_data_mtx;
static SemaphoreHandle_t s_kick;      // binary: request an immediate fetch
static weather_data_t s_data;
static bool s_have_data;

static void build_query(weather_query_t *q)
{
    const app_config_t *c = app_config_get();
    memset(q, 0, sizeof(*q));
    strlcpy(q->provider, c->provider, sizeof(q->provider));
    strlcpy(q->api_key, c->weather_key, sizeof(q->api_key));
    strlcpy(q->location, c->location, sizeof(q->location));
    strlcpy(q->city, c->city, sizeof(q->city));
    q->metric = c->units_metric;
    q->days = c->forecast_days;
}

static void do_fetch(void)
{
    if (!wifi_sta_connected()) {
        ESP_LOGW(TAG, "skip fetch: no network");
        return;
    }
    weather_query_t q;
    build_query(&q);

    weather_data_t fresh;
    if (weather_fetch(&q, &fresh) == ESP_OK) {
        xSemaphoreTake(s_data_mtx, portMAX_DELAY);
        s_data = fresh;
        s_have_data = true;
        xSemaphoreGive(s_data_mtx);
        ESP_LOGI(TAG, "updated: %s  %.0f deg  %s  (%d days)",
                 fresh.location_label, fresh.cur_temp, fresh.cur_cond, fresh.day_count);
    } else {
        ESP_LOGW(TAG, "fetch failed; keeping last data");
    }
}

static void weather_task(void *arg)
{
    (void)arg;
    // Wait for the clock so forecast-day timestamps land right.
    for (int i = 0; i < 60 && !time_sync_ntp_done(); i++) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    do_fetch();

    for (;;) {
        // Wake on either the refresh period or an explicit kick.
        xSemaphoreTake(s_kick, pdMS_TO_TICKS(REFRESH_PERIOD_MS));
        do_fetch();
    }
}

void app_weather_start(void)
{
    s_data_mtx = xSemaphoreCreateMutex();
    s_kick = xSemaphoreCreateBinary();
    xTaskCreate(weather_task, "weather", 12288, NULL, 4, NULL);
}

void app_weather_request_refresh(void)
{
    if (s_kick) {
        xSemaphoreGive(s_kick);
    }
}

bool app_weather_get(weather_data_t *out)
{
    if (!s_data_mtx || !out) {
        return false;
    }
    xSemaphoreTake(s_data_mtx, portMAX_DELAY);
    bool ok = s_have_data;
    if (ok) {
        *out = s_data;
    }
    xSemaphoreGive(s_data_mtx);
    return ok;
}
