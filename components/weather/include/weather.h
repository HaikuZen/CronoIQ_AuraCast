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
    char provider[24];   // "visualcrossing" | "openmeteo" | "openweathermap"
    char api_key[96];
    char location[80];   // "lat,lon" always works; VC/OWM also accept place names
    char city[48];       // optional display label; overrides auto place-name lookup
    bool metric;         // true: degC / km-h ; false: degF / mph
    int  days;           // 1..WEATHER_MAX_DAYS
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
} weather_data_t;

// Blocking HTTPS fetch + parse. Returns ESP_OK and fills *out on success.
// An unknown provider name falls back to Visual Crossing.
esp_err_t weather_fetch(const weather_query_t *q, weather_data_t *out);

// Maps a full condition string to a normalized key for icon selection. Returns a
// static string, one of:
//   "clear" "partly" "cloudy" "fog" "drizzle" "rain"
//   "snow" "sleet" "storm" "wind" "unknown"
const char *weather_condition_key(const char *text);

#ifdef __cplusplus
}
#endif
