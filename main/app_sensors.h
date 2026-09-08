#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Starts a task that polls the SHTC3 every ~20 s. `offset_dc` is a fixed
// correction in tenths of degC applied to the raw reading.
void app_sensors_start(int offset_dc);

// Latest indoor reading. Returns false until the first successful sample.
// temp_c is already corrected and always in degC; convert for display.
bool app_sensors_get(float *temp_c, float *rh_pct);

#ifdef __cplusplus
}
#endif
