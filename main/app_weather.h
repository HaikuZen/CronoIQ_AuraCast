#pragma once

#include <stdbool.h>
#include "weather.h"

#ifdef __cplusplus
extern "C" {
#endif

// Starts the weather worker. It waits for the first NTP sync, then fetches
// immediately and every 30 min afterwards.
void app_weather_start(void);

// Ask the worker to fetch again as soon as possible (e.g. BOOT long-press).
void app_weather_request_refresh(void);

// Copy the latest data (last good result is kept on failure). Returns false if
// nothing has ever been fetched.
bool app_weather_get(weather_data_t *out);

#ifdef __cplusplus
}
#endif
