#include "weather_internal.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "cJSON.h"

static const char *TAG = "wx.openmeteo";

static time_t day_string_to_epoch(const char *ymd)
{
    struct tm tm = { 0 };
    int y, m, d;
    if (sscanf(ymd, "%d-%d-%d", &y, &m, &d) != 3) {
        return 0;
    }
    tm.tm_year = y - 1900;
    tm.tm_mon = m - 1;
    tm.tm_mday = d;
    tm.tm_hour = 12;
    return mktime(&tm);
}

esp_err_t weather_provider_openmeteo(const weather_query_t *q, weather_data_t *out)
{
    double lat, lon;
    if (weather_parse_latlon(q->location, &lat, &lon) != ESP_OK) {
        ESP_LOGE(TAG, "location must be \"lat,lon\" for open-meteo (got '%s')", q->location);
        return ESP_ERR_INVALID_ARG;
    }

    char url[512];
    snprintf(url, sizeof(url),
             "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
             "&current=temperature_2m,relative_humidity_2m,apparent_temperature,weather_code"
             "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max"
             "&timezone=auto&forecast_days=%d&temperature_unit=%s&wind_speed_unit=%s",
             lat, lon, q->days,
             q->metric ? "celsius" : "fahrenheit",
             q->metric ? "kmh" : "mph");

    char *buf = heap_caps_malloc(WEATHER_HTTP_BUF_CAP, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) {
        return ESP_ERR_NO_MEM;
    }
    esp_err_t err = weather_http_get(url, buf, WEATHER_HTTP_BUF_CAP);
    if (err != ESP_OK) {
        heap_caps_free(buf);
        return err;
    }

    cJSON *root = cJSON_Parse(buf);
    heap_caps_free(buf);
    if (!root) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    snprintf(out->location_label, sizeof(out->location_label), "%.3f, %.3f", lat, lon);

    cJSON *cur = cJSON_GetObjectItem(root, "current");
    if (cur) {
        out->cur_temp = (float)cJSON_GetNumberValue(cJSON_GetObjectItem(cur, "temperature_2m"));
        out->cur_feels = (float)cJSON_GetNumberValue(cJSON_GetObjectItem(cur, "apparent_temperature"));
        cJSON *rh = cJSON_GetObjectItem(cur, "relative_humidity_2m");
        out->cur_humidity = cJSON_IsNumber(rh) ? (int)cJSON_GetNumberValue(rh) : -1;
        cJSON *wc = cJSON_GetObjectItem(cur, "weather_code");
        weather_wmo_text(cJSON_IsNumber(wc) ? (int)cJSON_GetNumberValue(wc) : -1,
                         out->cur_cond, sizeof(out->cur_cond));
    }

    cJSON *daily = cJSON_GetObjectItem(root, "daily");
    if (daily) {
        cJSON *times = cJSON_GetObjectItem(daily, "time");
        cJSON *codes = cJSON_GetObjectItem(daily, "weather_code");
        cJSON *tmax = cJSON_GetObjectItem(daily, "temperature_2m_max");
        cJSON *tmin = cJSON_GetObjectItem(daily, "temperature_2m_min");
        cJSON *pop = cJSON_GetObjectItem(daily, "precipitation_probability_max");
        int n = cJSON_GetArraySize(times);
        for (int i = 0; i < n && out->day_count < WEATHER_MAX_DAYS && out->day_count < q->days; i++) {
            weather_day_t *dd = &out->days[out->day_count++];
            dd->date = day_string_to_epoch(cJSON_GetStringValue(cJSON_GetArrayItem(times, i)));
            dd->tmax = (float)cJSON_GetNumberValue(cJSON_GetArrayItem(tmax, i));
            dd->tmin = (float)cJSON_GetNumberValue(cJSON_GetArrayItem(tmin, i));
            cJSON *p = cJSON_GetArrayItem(pop, i);
            dd->pop = cJSON_IsNumber(p) ? (int)cJSON_GetNumberValue(p) : -1;
            weather_wmo_text((int)cJSON_GetNumberValue(cJSON_GetArrayItem(codes, i)),
                             dd->cond, sizeof(dd->cond));
        }
    }

    cJSON_Delete(root);
    return (out->day_count > 0) ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}
