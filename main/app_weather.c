#include "app_weather.h"
#include "app_config.h"
#include "time_sync.h"
#include "wifi_sta.h"

#include <string.h>
#include <strings.h>
#include <time.h>

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
    // Only meaningful if weather.provider == "telegram" (primary source rather
    // than an overlay); harmless to set unconditionally for other providers.
    strlcpy(q->tg_bridge_url, c->telegram_bridge_url, sizeof(q->tg_bridge_url));
    strlcpy(q->tg_bridge_token, c->telegram_bridge_token, sizeof(q->tg_bridge_token));
    strlcpy(q->tg_locality, c->telegram_locality, sizeof(q->tg_locality));
}

// Overlays hyperlocal current conditions from the CML Telegram bot onto
// *fresh* (temp/feels/humidity/condition/location only; the primary
// provider's forecast days are left untouched). Returns true if the overlay
// produced usable data, so the caller can treat that alone as "ok" even if
// the primary provider fetch failed.
static bool apply_telegram_overlay(const app_config_t *c, weather_data_t *fresh)
{
    if (!c->telegram_enabled || !c->telegram_bridge_url[0] || !c->telegram_locality[0]) {
        return false;
    }
    // Already the primary source -> weather_fetch() handled it, nothing to layer.
    if (strcasecmp(c->provider, "telegram") == 0 || strcasecmp(c->provider, "cml") == 0) {
        return false;
    }

    weather_query_t tq = { 0 };
    strlcpy(tq.tg_bridge_url, c->telegram_bridge_url, sizeof(tq.tg_bridge_url));
    strlcpy(tq.tg_bridge_token, c->telegram_bridge_token, sizeof(tq.tg_bridge_token));
    strlcpy(tq.tg_locality, c->telegram_locality, sizeof(tq.tg_locality));
    tq.metric = c->units_metric;

    weather_data_t tg;
    if (weather_fetch_telegram_current(&tq, &tg) != ESP_OK) {
        ESP_LOGW(TAG, "telegram cml overlay failed; keeping primary current conditions");
        return false;
    }

    fresh->cur_temp = tg.cur_temp;
    fresh->cur_feels = tg.cur_feels;
    fresh->cur_humidity = tg.cur_humidity;
    fresh->cur_wind = tg.cur_wind;
    fresh->cur_precip = tg.cur_precip;
    strlcpy(fresh->cur_cond, tg.cur_cond, sizeof(fresh->cur_cond));
    strlcpy(fresh->location_label, tg.location_label, sizeof(fresh->location_label));
    strlcpy(fresh->cur_wind_dir, tg.cur_wind_dir, sizeof(fresh->cur_wind_dir));
    strlcpy(fresh->cur_asof, tg.cur_asof, sizeof(fresh->cur_asof));
    ESP_LOGI(TAG, "telegram cml overlay: %s  %.1f deg  %s",
             fresh->location_label, fresh->cur_temp, fresh->cur_cond);
    return true;
}

static void do_fetch(void)
{
    if (!wifi_sta_connected()) {
        ESP_LOGW(TAG, "skip fetch: no network");
        return;
    }
    const app_config_t *c = app_config_get();
    weather_query_t q;
    build_query(&q);

    weather_data_t fresh;
    bool ok = (weather_fetch(&q, &fresh) == ESP_OK);
    if (!ok) {
        ESP_LOGW(TAG, "primary provider fetch failed");
        memset(&fresh, 0, sizeof(fresh));
    }

    if (apply_telegram_overlay(c, &fresh)) {
        fresh.ok = true;
        fresh.updated = time(NULL);
        if (!ok) {
            strlcpy(fresh.provider, "telegram", sizeof(fresh.provider));
        }
        ok = true;
    }

    if (ok) {
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
