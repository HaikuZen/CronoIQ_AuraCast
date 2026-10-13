// Shared helpers between the weather dispatcher and the provider back-ends.
#pragma once

#include <stddef.h>
#include "esp_err.h"
#include "weather.h"

#define WEATHER_HTTP_BUF_CAP (28 * 1024)

// GET `url` over HTTPS (bundled root certs) into `buf` (NUL-terminated).
// Fails if the body would exceed cap-1 bytes.
esp_err_t weather_http_get(const char *url, char *buf, size_t cap);

// Full human description for a WMO weather-interpretation code (Open-Meteo).
void weather_wmo_text(int code, char *out, size_t cap);

// Reverse-geocode "lat,lon" to a "City, CC" label (keyless service).
esp_err_t weather_reverse_geocode(double lat, double lon, char *out, size_t cap);

// Provider back-ends.
esp_err_t weather_provider_visualcrossing(const weather_query_t *q, weather_data_t *out);
esp_err_t weather_provider_openmeteo(const weather_query_t *q, weather_data_t *out);
esp_err_t weather_provider_openweathermap(const weather_query_t *q, weather_data_t *out);
esp_err_t weather_provider_telegram_cml(const weather_query_t *q, weather_data_t *out);

// Split "lat,lon" into numbers. Returns ESP_OK on success.
esp_err_t weather_parse_latlon(const char *s, double *lat, double *lon);

// True if the string is only digits / sign / dot / comma / space (i.e. a raw
// coordinate rather than a place name).
bool weather_looks_like_coords(const char *s);
