#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "weather.h"
#include "weather_internal.h"

#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "cJSON.h"

static const char *TAG = "weather";

typedef struct {
    char  *buf;
    size_t cap;
    size_t len;
} resp_acc_t;

static esp_err_t http_evt(esp_http_client_event_t *evt)
{
    if (evt->event_id == HTTP_EVENT_ON_DATA) {
        resp_acc_t *acc = evt->user_data;
        if (acc->len + evt->data_len >= acc->cap) {
            ESP_LOGE(TAG, "response exceeds %u byte cap", (unsigned)acc->cap);
            return ESP_FAIL;
        }
        memcpy(acc->buf + acc->len, evt->data, evt->data_len);
        acc->len += evt->data_len;
        acc->buf[acc->len] = '\0';
    }
    return ESP_OK;
}

esp_err_t weather_http_get(const char *url, char *buf, size_t cap)
{
    resp_acc_t acc = { .buf = buf, .cap = cap, .len = 0 };
    buf[0] = '\0';

    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .event_handler = http_evt,
        .user_data = &acc,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 15000,
        .buffer_size = 2048,
        .buffer_size_tx = 2048,
    };
    esp_http_client_handle_t cli = esp_http_client_init(&cfg);
    if (!cli) {
        return ESP_FAIL;
    }
    esp_err_t err = esp_http_client_perform(cli);
    int status = esp_http_client_get_status_code(cli);
    esp_http_client_cleanup(cli);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "GET failed: %s", esp_err_to_name(err));
        return err;
    }
    if (status < 200 || status >= 300) {
        ESP_LOGE(TAG, "HTTP %d: %.120s", status, buf);
        return ESP_FAIL;
    }
    ESP_LOGD(TAG, "got %u bytes", (unsigned)acc.len);
    return ESP_OK;
}

esp_err_t weather_parse_latlon(const char *s, double *lat, double *lon)
{
    if (!s) {
        return ESP_ERR_INVALID_ARG;
    }
    char *end = NULL;
    double a = strtod(s, &end);
    if (end == s || !end) {
        return ESP_ERR_INVALID_ARG;
    }
    while (*end == ' ' || *end == ',' || *end == ';') {
        end++;
    }
    char *end2 = NULL;
    double b = strtod(end, &end2);
    if (end2 == end) {
        return ESP_ERR_INVALID_ARG;
    }
    *lat = a;
    *lon = b;
    return ESP_OK;
}

bool weather_looks_like_coords(const char *s)
{
    if (!s || !*s) {
        return false;
    }
    for (const char *p = s; *p; p++) {
        if (!(isdigit((unsigned char)*p) || *p == '.' || *p == ',' ||
              *p == '-' || *p == '+' || *p == ' ')) {
            return false;
        }
    }
    return true;
}

static bool has(const char *hay, const char *needle)
{
    return strcasestr(hay, needle) != NULL;
}

const char *weather_condition_key(const char *text)
{
    if (!text || !*text) {
        return "unknown";
    }
    if (has(text, "thunder") || has(text, "storm"))            return "storm";
    if (has(text, "sleet") || has(text, "freezing"))           return "sleet";
    if (has(text, "snow") || has(text, "flurr") || has(text, "ice") || has(text, "blizzard")) return "snow";
    if (has(text, "drizzle"))                                  return "drizzle";
    if (has(text, "rain") || has(text, "shower"))              return "rain";
    if (has(text, "fog") || has(text, "mist") || has(text, "haze")) return "fog";
    if (has(text, "wind") || has(text, "gale") || has(text, "breez")) return "wind";
    if (has(text, "partly") || has(text, "partial") || has(text, "part-") ||
        has(text, "few clouds") || has(text, "scattered") ||
        has(text, "mainly clear"))                             return "partly";
    if (has(text, "overcast") || has(text, "cloud"))           return "cloudy";
    if (has(text, "clear") || has(text, "sun") || has(text, "fair")) return "clear";
    return "unknown";
}

void weather_wmo_text(int code, char *out, size_t cap)
{
    const char *r;
    switch (code) {
    case 0:  r = "Clear sky"; break;
    case 1:  r = "Mainly clear"; break;
    case 2:  r = "Partly cloudy"; break;
    case 3:  r = "Overcast"; break;
    case 45: r = "Fog"; break;
    case 48: r = "Depositing rime fog"; break;
    case 51: r = "Light drizzle"; break;
    case 53: r = "Moderate drizzle"; break;
    case 55: r = "Dense drizzle"; break;
    case 56: r = "Light freezing drizzle"; break;
    case 57: r = "Dense freezing drizzle"; break;
    case 61: r = "Slight rain"; break;
    case 63: r = "Moderate rain"; break;
    case 65: r = "Heavy rain"; break;
    case 66: r = "Light freezing rain"; break;
    case 67: r = "Heavy freezing rain"; break;
    case 71: r = "Slight snow fall"; break;
    case 73: r = "Moderate snow fall"; break;
    case 75: r = "Heavy snow fall"; break;
    case 77: r = "Snow grains"; break;
    case 80: r = "Slight rain showers"; break;
    case 81: r = "Moderate rain showers"; break;
    case 82: r = "Violent rain showers"; break;
    case 85: r = "Slight snow showers"; break;
    case 86: r = "Heavy snow showers"; break;
    case 95: r = "Thunderstorm"; break;
    case 96: r = "Thunderstorm with slight hail"; break;
    case 99: r = "Thunderstorm with heavy hail"; break;
    default: r = "Unknown"; break;
    }
    strlcpy(out, r, cap);
}

esp_err_t weather_reverse_geocode(double lat, double lon, char *out, size_t cap)
{
    char url[192];
    snprintf(url, sizeof(url),
             "https://api.bigdatacloud.net/data/reverse-geocode-client"
             "?latitude=%.4f&longitude=%.4f&localityLanguage=en", lat, lon);

    char *buf = heap_caps_malloc(8192, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) {
        return ESP_ERR_NO_MEM;
    }
    esp_err_t err = weather_http_get(url, buf, 8192);
    if (err != ESP_OK) {
        heap_caps_free(buf);
        return err;
    }
    cJSON *root = cJSON_Parse(buf);
    heap_caps_free(buf);
    if (!root) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    const char *city = cJSON_GetStringValue(cJSON_GetObjectItem(root, "city"));
    if (!city || !*city) {
        city = cJSON_GetStringValue(cJSON_GetObjectItem(root, "locality"));
    }
    if (!city || !*city) {
        city = cJSON_GetStringValue(cJSON_GetObjectItem(root, "principalSubdivision"));
    }
    const char *cc = cJSON_GetStringValue(cJSON_GetObjectItem(root, "countryCode"));

    esp_err_t ret = ESP_ERR_NOT_FOUND;
    if (city && *city) {
        if (cc && *cc) {
            snprintf(out, cap, "%s, %s", city, cc);
        } else {
            strlcpy(out, city, cap);
        }
        ret = ESP_OK;
    }
    cJSON_Delete(root);
    return ret;
}

static void resolve_place_name(const weather_query_t *q, weather_data_t *out)
{
    if (q->city[0]) {
        strlcpy(out->location_label, q->city, sizeof(out->location_label));
        return;
    }
    if (out->location_label[0] && !weather_looks_like_coords(out->location_label)) {
        return;  // provider already gave a real name
    }
    double lat, lon;
    if (weather_parse_latlon(q->location, &lat, &lon) != ESP_OK) {
        return;
    }
    char label[64];
    if (weather_reverse_geocode(lat, lon, label, sizeof(label)) == ESP_OK) {
        ESP_LOGI(TAG, "reverse-geocoded %s -> %s", q->location, label);
        strlcpy(out->location_label, label, sizeof(out->location_label));
    }
}

esp_err_t weather_fetch(const weather_query_t *q, weather_data_t *out)
{
    if (!q || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));
    out->cur_wind = -1;
    out->cur_precip = -1;

    weather_query_t norm = *q;
    if (norm.days < 1) {
        norm.days = 1;
    }
    if (norm.days > WEATHER_MAX_DAYS) {
        norm.days = WEATHER_MAX_DAYS;
    }

    esp_err_t (*fn)(const weather_query_t *, weather_data_t *) = weather_provider_visualcrossing;
    const char *name = "visualcrossing";
    if (strcasecmp(norm.provider, "openmeteo") == 0 || strcasecmp(norm.provider, "open-meteo") == 0) {
        fn = weather_provider_openmeteo;
        name = "openmeteo";
    } else if (strcasecmp(norm.provider, "openweathermap") == 0 || strcasecmp(norm.provider, "owm") == 0) {
        fn = weather_provider_openweathermap;
        name = "openweathermap";
    } else if (strcasecmp(norm.provider, "telegram") == 0 || strcasecmp(norm.provider, "cml") == 0) {
        fn = weather_provider_telegram_cml;
        name = "telegram";
    } else if (norm.provider[0] && strcasecmp(norm.provider, "visualcrossing") != 0) {
        ESP_LOGW(TAG, "unknown provider '%s', using visualcrossing", norm.provider);
    }

    ESP_LOGI(TAG, "fetch via %s (%s, %d day)", name, norm.location, norm.days);
    esp_err_t err = fn(&norm, out);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "provider %s failed: %s", name, esp_err_to_name(err));
        return err;
    }

    out->ok = true;
    out->updated = time(NULL);
    strlcpy(out->provider, name, sizeof(out->provider));
    if (out->cur_cond[0] == '\0') {
        strlcpy(out->cur_cond, "Unknown", sizeof(out->cur_cond));
    }
    resolve_place_name(&norm, out);
    return ESP_OK;
}

esp_err_t weather_fetch_telegram_current(const weather_query_t *q, weather_data_t *out)
{
    if (!q || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));
    out->cur_wind = -1;
    out->cur_precip = -1;
    esp_err_t err = weather_provider_telegram_cml(q, out);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "telegram cml fetch failed: %s", esp_err_to_name(err));
        return err;
    }
    out->ok = true;
    out->updated = time(NULL);
    strlcpy(out->provider, "telegram", sizeof(out->provider));
    return ESP_OK;
}
