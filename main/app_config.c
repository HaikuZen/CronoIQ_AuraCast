#include "app_config.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "esp_log.h"
#include "cJSON.h"
#include "sdcard.h"

// ---------------------------------------------------------------------------
// Compiled-in defaults (used when the SD card / config.json is missing).
// Edit these for a no-SD build.
// ---------------------------------------------------------------------------
#define DEF_WIFI_SSID     "my-ssid"
#define DEF_WIFI_PASS     "my-pass"
#define DEF_TZ            "CET-1CEST,M3.5.0,M10.5.0/3"   // Central Europe
#define DEF_NTP           "pool.ntp.org"
#define DEF_PROVIDER      "visualcrossing"
#define DEF_WEATHER_KEY   ""
#define DEF_LOCATION      "45.4642,9.1900"               // Milan, IT
#define DEF_CITY          "Milano"                             // "" = auto place-name
#define DEF_UNITS_METRIC  true
#define DEF_FORECAST_DAYS 3
#define DEF_TEMP_OFFSET   -40                            // -4.0 C, matches Waveshare demo
#define DEF_TG_ENABLED     false
#define DEF_TG_BRIDGE_URL  ""
#define DEF_TG_BRIDGE_TOKEN ""
#define DEF_TG_LOCALITY    ""

#define CONFIG_PATH  SDCARD_MOUNT_POINT "/config.json"

static const char *TAG = "app_config";
static app_config_t s_cfg;

static void set_defaults(app_config_t *c)
{
    memset(c, 0, sizeof(*c));
    strlcpy(c->wifi_ssid, DEF_WIFI_SSID, sizeof(c->wifi_ssid));
    strlcpy(c->wifi_pass, DEF_WIFI_PASS, sizeof(c->wifi_pass));
    strlcpy(c->tz, DEF_TZ, sizeof(c->tz));
    strlcpy(c->ntp_server, DEF_NTP, sizeof(c->ntp_server));
    strlcpy(c->provider, DEF_PROVIDER, sizeof(c->provider));
    strlcpy(c->weather_key, DEF_WEATHER_KEY, sizeof(c->weather_key));
    strlcpy(c->location, DEF_LOCATION, sizeof(c->location));
    strlcpy(c->city, DEF_CITY, sizeof(c->city));
    c->units_metric = DEF_UNITS_METRIC;
    c->forecast_days = DEF_FORECAST_DAYS;
    c->temp_offset_dc = DEF_TEMP_OFFSET;
    c->telegram_enabled = DEF_TG_ENABLED;
    strlcpy(c->telegram_bridge_url, DEF_TG_BRIDGE_URL, sizeof(c->telegram_bridge_url));
    strlcpy(c->telegram_bridge_token, DEF_TG_BRIDGE_TOKEN, sizeof(c->telegram_bridge_token));
    strlcpy(c->telegram_locality, DEF_TG_LOCALITY, sizeof(c->telegram_locality));
    c->from_sd = false;
}

static void copy_str(const cJSON *obj, const char *key, char *dst, size_t cap, bool *touched)
{
    const cJSON *it = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsString(it) && it->valuestring) {
        strlcpy(dst, it->valuestring, cap);
        *touched = true;
    }
}

static void copy_int(const cJSON *obj, const char *key, int *dst, bool *touched)
{
    const cJSON *it = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsNumber(it)) {
        *dst = it->valueint;
        *touched = true;
    }
}

static void copy_bool(const cJSON *obj, const char *key, bool *dst, bool *touched)
{
    const cJSON *it = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsBool(it)) {
        *dst = cJSON_IsTrue(it);
        *touched = true;
    }
}

static char *read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > 8192) {
        fclose(f);
        ESP_LOGW(TAG, "config.json size %ld out of range", sz);
        return NULL;
    }
    char *buf = malloc(sz + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    size_t got = fread(buf, 1, sz, f);
    fclose(f);
    buf[got] = '\0';
    return buf;
}

static bool apply_json(app_config_t *c, const char *json)
{
    cJSON *root = cJSON_Parse(json);
    if (!root) {
        const char *e = cJSON_GetErrorPtr();
        ESP_LOGE(TAG, "config.json parse error near: %.40s", e ? e : "?");
        return false;
    }

    bool touched = false;
    const cJSON *wifi = cJSON_GetObjectItemCaseSensitive(root, "wifi");
    if (wifi) {
        copy_str(wifi, "ssid", c->wifi_ssid, sizeof(c->wifi_ssid), &touched);
        copy_str(wifi, "password", c->wifi_pass, sizeof(c->wifi_pass), &touched);
    }

    const cJSON *w = cJSON_GetObjectItemCaseSensitive(root, "weather");
    if (w) {
        copy_str(w, "provider", c->provider, sizeof(c->provider), &touched);
        copy_str(w, "api_key", c->weather_key, sizeof(c->weather_key), &touched);
        copy_str(w, "location", c->location, sizeof(c->location), &touched);
        copy_int(w, "forecast_days", &c->forecast_days, &touched);
        copy_bool(w, "metric", &c->units_metric, &touched);
    }

    const cJSON *t = cJSON_GetObjectItemCaseSensitive(root, "time");
    if (t) {
        copy_str(t, "tz", c->tz, sizeof(c->tz), &touched);
        copy_str(t, "ntp_server", c->ntp_server, sizeof(c->ntp_server), &touched);
    }

    const cJSON *s = cJSON_GetObjectItemCaseSensitive(root, "sensor");
    if (s) {
        copy_int(s, "temp_offset_dc", &c->temp_offset_dc, &touched);
    }

    const cJSON *tg = cJSON_GetObjectItemCaseSensitive(root, "telegram");
    if (tg) {
        copy_bool(tg, "enabled", &c->telegram_enabled, &touched);
        copy_str(tg, "bridge_url", c->telegram_bridge_url, sizeof(c->telegram_bridge_url), &touched);
        copy_str(tg, "bridge_token", c->telegram_bridge_token, sizeof(c->telegram_bridge_token), &touched);
        copy_str(tg, "locality", c->telegram_locality, sizeof(c->telegram_locality), &touched);
    }

    cJSON_Delete(root);

    if (c->forecast_days < 1) c->forecast_days = 1;
    if (c->forecast_days > 5) c->forecast_days = 5;
    return touched;
}

void app_config_load(void)
{
    set_defaults(&s_cfg);

    if (sdcard_mount() != ESP_OK) {
        ESP_LOGW(TAG, "no SD card — using compiled defaults");
        return;
    }
    char *json = read_file(CONFIG_PATH);
    if (!json) {
        ESP_LOGW(TAG, "%s not found — using compiled defaults", CONFIG_PATH);
        return;
    }
    bool touched = apply_json(&s_cfg, json);
    free(json);

    s_cfg.from_sd = touched;
    ESP_LOGI(TAG, "config: source=%s provider=%s location=%s days=%d metric=%d tz=%s",
             touched ? "SD" : "defaults", s_cfg.provider, s_cfg.location,
             s_cfg.forecast_days, s_cfg.units_metric, s_cfg.tz);
    ESP_LOGI(TAG, "wifi ssid=\"%s\" key=%s", s_cfg.wifi_ssid,
             s_cfg.weather_key[0] ? "set" : "empty");
    ESP_LOGI(TAG, "telegram cml overlay: %s locality=\"%s\" bridge=%s",
             s_cfg.telegram_enabled ? "enabled" : "disabled", s_cfg.telegram_locality,
             s_cfg.telegram_bridge_url[0] ? s_cfg.telegram_bridge_url : "unset");
}

const app_config_t *app_config_get(void)
{
    return &s_cfg;
}
