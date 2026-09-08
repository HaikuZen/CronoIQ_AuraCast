// microSD card over SDMMC (1-bit): CLK GPIO38, CMD GPIO21, D0 GPIO39.
#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SDCARD_MOUNT_POINT "/sdcard"

// Mounts the FAT filesystem. Returns ESP_OK on success; a missing or unformatted
// card returns an error and the caller should fall back to compiled defaults.
esp_err_t sdcard_mount(void);

bool sdcard_mounted(void);

#ifdef __cplusplus
}
#endif
