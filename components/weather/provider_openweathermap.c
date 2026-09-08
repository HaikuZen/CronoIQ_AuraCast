#include "weather_internal.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "cJSON.h"

static const char *TAG = "wx.openweathermap";

// Join the "description" of every entry: "light rain, mist".
static void text_from_weather_array(cJSON *arr, char *out, size_t cap)
{
    out[0] = '\0';
    int n = cJSON_GetArraySize(arr);
    for (int i = 0; i < n; i++) {
        cJSON *w = cJSON_GetArrayItem(arr, i);
        const char *d = cJSON_GetStringValue(cJSON_GetObjectItem(w, "description"));
        if (!d || !*d) {
            d = cJSON_GetStringValue(cJSON_GetObjectItem(w, "main"));
        }
        if (!d || !*d) {
            continue;
        }
        if (out[0]) {
            strlcat(out, ", ", cap);
        }
        strlcat(out, d, cap);
    }
}

esp_err_t weather_provider_openweathermap(const weather_query_t *q, weather_data_t *out)
{
    if (q->api_key[0] == '\0') {
        ESP_LOGE(TAG, "no api_key configured");
        return ESP_ERR_INVALID_ARG;
    }
    double lat, lon;
    if (weather_parse_latlon(q->location, &lat, &lon) != ESP_OK) {
        ESP_LOGE(TAG, "location must be \"lat,lon\" for openweathermap (got '%s')", q->location);
        return ESP_ERR_INVALID_ARG;
    }

    char url[512];
    snprintf(url, sizeof(url),
             "https://api.openweathermap.org/data/3.0/onecall?lat=%.4f&lon=%.4f"
             "&exclude=minutely,hourly,alerts&units=%s&appid=%s",
             lat, lon, q->metric ? "metric" : "imperial", q->api_key);

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
        out->cur_temp = (float)cJSON_GetNumberValue(cJSON_GetObjectItem(cur, "temp"));
        out->cur_feels = (float)cJSON_GetNumberValue(cJSON_GetObjectItem(cur, "feels_like"));
        cJSON *h = cJSON_GetObjectItem(cur, "humidity");
        out->cur_humidity = cJSON_IsNumber(h) ? (int)cJSON_GetNumberValue(h) : -1;
        text_from_weather_array(cJSON_GetObjectItem(cur, "weather"), out->cur_cond, sizeof(out->cur_cond));
    }

    cJSON *daily = cJSON_GetObjectItem(root, "daily");
    int n = cJSON_GetArraySize(daily);
    for (int i = 0; i < n && out->day_count < WEATHER_MAX_DAYS && out->day_count < q->days; i++) {
        cJSON *d = cJSON_GetArrayItem(daily, i);
        weather_day_t *dd = &out->days[out->day_count++];
        dd->date = (time_t)cJSON_GetNumberValue(cJSON_GetObjectItem(d, "dt"));
        cJSON *temp = cJSON_GetObjectItem(d, "temp");
        dd->tmax = (float)cJSON_GetNumberValue(cJSON_GetObjectItem(temp, "max"));
        dd->tmin = (float)cJSON_GetNumberValue(cJSON_GetObjectItem(temp, "min"));
        cJSON *pop = cJSON_GetObjectItem(d, "pop");
        dd->pop = cJSON_IsNumber(pop) ? (int)(cJSON_GetNumberValue(pop) * 100.0 + 0.5) : -1;
        text_from_weather_array(cJSON_GetObjectItem(d, "weather"), dd->cond, sizeof(dd->cond));
    }

    cJSON_Delete(root);
    return (out->day_count > 0) ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}
