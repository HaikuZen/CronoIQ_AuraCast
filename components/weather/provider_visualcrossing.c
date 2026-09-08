#include "weather_internal.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "cJSON.h"

static const char *TAG = "wx.visualcrossing";

// Percent-encode everything that is not an unreserved URL char.
static void url_encode(const char *in, char *out, size_t cap)
{
    static const char *hex = "0123456789ABCDEF";
    size_t o = 0;
    for (const unsigned char *p = (const unsigned char *)in; *p && o + 4 < cap; p++) {
        unsigned char c = *p;
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            out[o++] = (char)c;
        } else {
            out[o++] = '%';
            out[o++] = hex[c >> 4];
            out[o++] = hex[c & 0x0F];
        }
    }
    out[o] = '\0';
}

esp_err_t weather_provider_visualcrossing(const weather_query_t *q, weather_data_t *out)
{
    if (q->api_key[0] == '\0') {
        ESP_LOGE(TAG, "no api_key configured");
        return ESP_ERR_INVALID_ARG;
    }

    char loc_enc[160];
    url_encode(q->location, loc_enc, sizeof(loc_enc));

    char url[640];
    snprintf(url, sizeof(url),
             "https://weather.visualcrossing.com/VisualCrossingWebServices/rest/services/timeline/%s"
             "?unitGroup=%s&include=days,current"
             "&elements=datetimeEpoch,tempmax,tempmin,temp,feelslike,humidity,precipprob,conditions"
             "&key=%s&contentType=json",
             loc_enc, q->metric ? "metric" : "us", q->api_key);

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

    const char *addr = cJSON_GetStringValue(cJSON_GetObjectItem(root, "resolvedAddress"));
    if (addr) {
        strlcpy(out->location_label, addr, sizeof(out->location_label));
    }

    cJSON *cur = cJSON_GetObjectItem(root, "currentConditions");
    if (cur) {
        out->cur_temp = (float)cJSON_GetNumberValue(cJSON_GetObjectItem(cur, "temp"));
        out->cur_feels = (float)cJSON_GetNumberValue(cJSON_GetObjectItem(cur, "feelslike"));
        cJSON *h = cJSON_GetObjectItem(cur, "humidity");
        out->cur_humidity = cJSON_IsNumber(h) ? (int)cJSON_GetNumberValue(h) : -1;
        const char *cc = cJSON_GetStringValue(cJSON_GetObjectItem(cur, "conditions"));
        if (cc && *cc) {
            strlcpy(out->cur_cond, cc, sizeof(out->cur_cond));
        }
    }

    cJSON *days = cJSON_GetObjectItem(root, "days");
    int n = cJSON_GetArraySize(days);
    for (int i = 0; i < n && out->day_count < WEATHER_MAX_DAYS && out->day_count < q->days; i++) {
        cJSON *d = cJSON_GetArrayItem(days, i);
        weather_day_t *dd = &out->days[out->day_count++];
        dd->date = (time_t)cJSON_GetNumberValue(cJSON_GetObjectItem(d, "datetimeEpoch"));
        dd->tmax = (float)cJSON_GetNumberValue(cJSON_GetObjectItem(d, "tempmax"));
        dd->tmin = (float)cJSON_GetNumberValue(cJSON_GetObjectItem(d, "tempmin"));
        cJSON *p = cJSON_GetObjectItem(d, "precipprob");
        dd->pop = cJSON_IsNumber(p) ? (int)cJSON_GetNumberValue(p) : -1;
        const char *dc = cJSON_GetStringValue(cJSON_GetObjectItem(d, "conditions"));
        if (dc && *dc) {
            strlcpy(dd->cond, dc, sizeof(dd->cond));
        }
    }

    // Current block sometimes missing precise "now" — fall back to day 0.
    if (out->cur_cond[0] == '\0' && out->day_count > 0) {
        strlcpy(out->cur_cond, out->days[0].cond, sizeof(out->cur_cond));
    }

    cJSON_Delete(root);
    return (out->day_count > 0) ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}
