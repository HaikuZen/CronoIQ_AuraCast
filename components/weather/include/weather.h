// Pluggable weather-forecast providers. Default: Visual Crossing.
#pragma once

#include <time.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WEATHER_MAX_DAYS 5

typedef struct {
    char provider[24];   // "visualcrossing" | "openmeteo" | "openweathermap" | "telegram"
    char api_key[96];
    char location[80];   // "lat,lon" always works; VC/OWM also accept place names
    char city[48];       // optional display label; overrides auto place-name lookup
    bool metric;         // true: degC / km-h ; false: degF / mph
    int  days;           // 1..WEATHER_MAX_DAYS
    // "telegram" provider only: base URL of the self-hosted bridge that holds
    // the real Telegram (MTProto) session and talks to @CML_robot for us --
    // see tools/telegram_cml_bridge.py. No bot/chat_id needed on this side.
    char tg_bridge_url[80];   // e.g. "http://192.168.1.50:8765"
    char tg_bridge_token[48]; // optional shared secret, "" = bridge has no auth
    char tg_locality[48];     // passed to the bridge as "/realtime <locality>"
} weather_query_t;

typedef struct {
    time_t date;         // local midnight of the forecast day
    float  tmin;
    float  tmax;
    int    pop;          // precipitation probability, 0..100 (-1 if unknown)
    char   cond[48];     // full human text, e.g. "Rain, Partially cloudy"
} weather_day_t;

typedef struct {
    bool   ok;
    char   provider[24]; // provider that produced this data
    char   location_label[64];
    float  cur_temp;
    float  cur_feels;
    int    cur_humidity; // %RH, -1 if unknown
    char   cur_cond[64]; // full human text
    time_t updated;      // wall-clock time of this fetch
    int    day_count;
    weather_day_t days[WEATHER_MAX_DAYS];

    // Structured current wind/precip + the source's own "as of" time.
    // Only the "telegram" provider fills these today (parsed straight out of
    // @CML_robot's reply); every other provider leaves them at the -1/""
    // "unknown" defaults set by weather_fetch()/weather_fetch_telegram_current().
    float  cur_wind;      // km/h or mph per q->metric, -1 if unknown
    char   cur_wind_dir[8]; // compass direction, e.g. "NW"; "" if unknown
    float  cur_precip;    // today's precipitation, mm or in per q->metric, -1 if unknown
    char   cur_asof[8];   // station's own report time, "HH:MM"; "" if unknown
} weather_data_t;

// Blocking HTTPS fetch + parse. Returns ESP_OK and fills *out on success.
// An unknown provider name falls back to Visual Crossing.
esp_err_t weather_fetch(const weather_query_t *q, weather_data_t *out);

// Blocking fetch of *current conditions only* via tg_bridge_url (a small
// self-hosted HTTP bridge that holds a real Telegram login and relays
// "/realtime <tg_locality>" to @CML_robot -- see
// tools/telegram_cml_bridge.py). Fills
// cur_temp/cur_feels/cur_humidity/cur_cond/location_label; day_count is
// always 0 (the bot gives no forecast). Independent of weather_fetch() /
// q->provider, so it can be layered on top of any primary provider's
// forecast as a hyperlocal current-conditions overlay.
esp_err_t weather_fetch_telegram_current(const weather_query_t *q, weather_data_t *out);

// Maps a full condition string to a normalized key for icon selection. Returns a
// static string, one of:
//   "clear" "partly" "cloudy" "fog" "drizzle" "rain"
//   "snow" "sleet" "storm" "wind" "unknown"
const char *weather_condition_key(const char *text);

#ifdef __cplusplus
}
#endif
