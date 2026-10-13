// Hyperlocal current conditions from the CML (Comunita Meteo Lombardia)
// amateur weather-station network, via its Telegram bot @CML_robot.
//
// @CML_robot only replies to a real Telegram user account talking to it in a
// 1:1 chat with "/realtime" followed by the locality as a *separate*
// follow-up message (many bots ignore other bots, and there's no sane way to
// speak Telegram's MTProto protocol from ESP32 firmware anyway). So this
// provider does NOT talk to Telegram directly: it calls a small self-hosted
// HTTP/JSON bridge (tools/telegram_cml_bridge.py) that holds the real login
// (via credentials from https://my.telegram.org) and does the Telegram side
// for us. From here it's just another HTTPS/HTTP GET, same as every other
// provider in this component.
//
//   GET <tg_bridge_url>/realtime?locality=<enc>[&token=<tg_bridge_token>]
//   -> {"ok": true,  "text": "<raw @CML_robot reply>"}
//   -> {"ok": false, "error": "..."}
//
// Example reply text this parses:
//   Milano Arbe (MI) - 130m slm
//   dati del 13/10/2026 ore 8:21 locali
//   Temperatura: 15.6°C
//   Umidità: 71%
//   Vento: 0.0 km/h da ENE
//   Prec. odierne: 0.0 mm
//
// No forecast is available from this source: day_count is always 0. See the
// README for how to run the bridge.

#include "weather_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "cJSON.h"

static const char *TAG = "wx.telegram_cml";

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

// Finds `key` in `text` and parses the float that follows it, skipping over
// anything between the key and the number -- not just ':'/spaces: some keys
// are immediately followed by accented text before the colon ("Umidit" + the
// rest of "à: 71%", "Prec." + " odierne: 0.0 mm"), and those extra bytes
// aren't ':'/' ' either. Stop at a line break so a missing value on this
// key's own line can't accidentally skip onto a number from the next line.
// Returns false if `key` isn't present or no number follows before the EOL.
static bool num_after(const char *text, const char *key, float *out)
{
    const char *p = strstr(text, key);
    if (!p) {
        return false;
    }
    p += strlen(key);
    while (*p && *p != '\n' && *p != '\r' && *p != '-' && !(*p >= '0' && *p <= '9')) {
        p++;
    }
    char *end = NULL;
    float v = strtof(p, &end);
    if (end == p) {
        return false;
    }
    *out = v;
    return true;
}

// Wind direction token after "... da <DIR>" (e.g. "0.0 km/h da ENE" -> "ENE").
static bool wind_dir_after(const char *text, const char *key, char *out, size_t cap)
{
    const char *p = strstr(text, key);
    if (!p) {
        return false;
    }
    p = strstr(p, " da ");
    if (!p) {
        return false;
    }
    p += 4;
    size_t n = 0;
    while (*p && *p != '\n' && *p != '\r' && *p != ' ' && n + 1 < cap) {
        out[n++] = *p++;
    }
    out[n] = '\0';
    return n > 0;
}

// First whitespace-delimited token after `key`, e.g. "ore 16:26 locali" -> "16:26".
static bool token_after(const char *text, const char *key, char *out, size_t cap)
{
    const char *p = strstr(text, key);
    if (!p) {
        return false;
    }
    p += strlen(key);
    while (*p == ' ') {
        p++;
    }
    size_t n = 0;
    while (*p && *p != ' ' && *p != '\n' && *p != '\r' && n + 1 < cap) {
        out[n++] = *p++;
    }
    out[n] = '\0';
    return n > 0;
}

// @CML_robot wraps the station name in Telegram markdown ("**name**") and
// follows it with " - <elevation>m slm" on the same line -- strip both so
// location_label is just the plain place name.
static void clean_location(char *s)
{
    char *src = s, *dst = s;
    while (*src) {
        if (src[0] == '*' && src[1] == '*') {
            src += 2;
            continue;
        }
        *dst++ = *src++;
    }
    *dst = '\0';
    char *dash = strstr(s, " - ");
    if (dash) {
        *dash = '\0';
    }
}

static esp_err_t parse_reply_text(const char *json, char *text_out, size_t text_cap)
{
    cJSON *root = cJSON_Parse(json);
    if (!root) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    bool ok = cJSON_IsTrue(cJSON_GetObjectItem(root, "ok"));
    const char *text = cJSON_GetStringValue(cJSON_GetObjectItem(root, "text"));
    esp_err_t err;
    if (ok && text && *text) {
        strlcpy(text_out, text, text_cap);
        err = ESP_OK;
    } else {
        const char *desc = cJSON_GetStringValue(cJSON_GetObjectItem(root, "error"));
        ESP_LOGW(TAG, "bridge: %s", desc ? desc : "no text in reply");
        err = ESP_ERR_INVALID_RESPONSE;
    }
    cJSON_Delete(root);
    return err;
}

esp_err_t weather_provider_telegram_cml(const weather_query_t *q, weather_data_t *out)
{
    if (!q->tg_bridge_url[0] || !q->tg_locality[0]) {
        ESP_LOGE(TAG, "bridge_url / locality not configured");
        return ESP_ERR_INVALID_ARG;
    }

    char loc_enc[160];
    url_encode(q->tg_locality, loc_enc, sizeof(loc_enc));

    char url[640];
    if (q->tg_bridge_token[0]) {
        char tok_enc[160];
        url_encode(q->tg_bridge_token, tok_enc, sizeof(tok_enc));
        snprintf(url, sizeof(url), "%s/realtime?locality=%s&token=%s",
                 q->tg_bridge_url, loc_enc, tok_enc);
    } else {
        snprintf(url, sizeof(url), "%s/realtime?locality=%s", q->tg_bridge_url, loc_enc);
    }

    char *buf = heap_caps_malloc(WEATHER_HTTP_BUF_CAP, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) {
        return ESP_ERR_NO_MEM;
    }
    esp_err_t err = weather_http_get(url, buf, WEATHER_HTTP_BUF_CAP);
    if (err != ESP_OK) {
        heap_caps_free(buf);
        return err;
    }

    char text[640];
    err = parse_reply_text(buf, text, sizeof(text));
    heap_caps_free(buf);
    if (err != ESP_OK) {
        return err;
    }

    // First line: the station's own name/elevation, e.g. "**Milano Arbe (MI)** - 130m slm".
    char first_line[64] = {0};
    const char *nl = strchr(text, '\n');
    size_t n = nl ? (size_t)(nl - text) : strlen(text);
    if (n >= sizeof(first_line)) {
        n = sizeof(first_line) - 1;
    }
    memcpy(first_line, text, n);
    first_line[n] = '\0';
    clean_location(first_line);   // -> "Milano Arbe (MI)"

    char asof[8] = {0};
    token_after(text, "ore ", asof, sizeof(asof));   // "ore 16:26 locali" -> "16:26"

    float temp_c = 0, humidity = -1, wind_kmh = 0, precip_mm = 0;
    bool have_temp = num_after(text, "Temperatura", &temp_c);
    bool have_humi = num_after(text, "Umidit", &humidity);
    bool have_wind = num_after(text, "Vento", &wind_kmh);
    bool have_prec = num_after(text, "Prec.", &precip_mm);
    char wind_dir[8] = {0};
    bool have_dir = have_wind && wind_dir_after(text, "Vento", wind_dir, sizeof(wind_dir));

    if (!have_temp) {
        ESP_LOGW(TAG, "reply didn't contain a parsable \"Temperatura\" field");
        return ESP_ERR_INVALID_RESPONSE;
    }

    const char *unit_t = q->metric ? "C" : "F";
    const char *unit_w = q->metric ? "km/h" : "mph";
    const char *unit_p = q->metric ? "mm" : "in";
    float disp_temp = q->metric ? temp_c : (temp_c * 9.0f / 5.0f + 32.0f);
    float disp_wind = q->metric ? wind_kmh : (wind_kmh * 0.621371f);
    float disp_prec = q->metric ? precip_mm : (precip_mm / 25.4f);

    out->cur_temp = disp_temp;
    out->cur_feels = disp_temp;   // no apparent-temperature figure from this source
    out->cur_humidity = have_humi ? (int)(humidity + 0.5f) : -1;
    strlcpy(out->location_label, first_line[0] ? first_line : q->tg_locality,
            sizeof(out->location_label));
    strlcpy(out->cur_asof, asof, sizeof(out->cur_asof));
    out->cur_wind = have_wind ? disp_wind : -1;
    out->cur_precip = have_prec ? disp_prec : -1;
    strlcpy(out->cur_wind_dir, have_dir ? wind_dir : "", sizeof(out->cur_wind_dir));

    if (have_prec && precip_mm > 0.2f) {
        if (have_dir) {
            snprintf(out->cur_cond, sizeof(out->cur_cond), "Rain today %.1f%s, wind %.0f%s %s %s",
                     disp_prec, unit_p, disp_wind, unit_w, wind_dir, unit_t);
        } else {
            snprintf(out->cur_cond, sizeof(out->cur_cond), "Rain today %.1f%s", disp_prec, unit_p);
        }
    } else if (have_wind && wind_kmh >= 10.0f) {
        snprintf(out->cur_cond, sizeof(out->cur_cond), "Windy, %.0f%s %s", disp_wind, unit_w,
                 have_dir ? wind_dir : "");
    } else if (have_wind) {
        snprintf(out->cur_cond, sizeof(out->cur_cond), "Local station, wind %.0f%s %s", disp_wind,
                 unit_w, have_dir ? wind_dir : "");
    } else {
        strlcpy(out->cur_cond, "Local station data", sizeof(out->cur_cond));
    }

    out->day_count = 0;   // @CML_robot gives no forecast
    return ESP_OK;
}
