#include "sdcard.h"

#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include "esp_log.h"

static const char *TAG = "sdcard";

#define SD_CLK GPIO_NUM_38
#define SD_CMD GPIO_NUM_21
#define SD_D0  GPIO_NUM_39

static sdmmc_card_t *s_card;

esp_err_t sdcard_mount(void)
{
    if (s_card) {
        return ESP_OK;
    }
    esp_vfs_fat_sdmmc_mount_config_t mount_cfg = {
        .format_if_mount_failed = false,
        .max_files = 4,
        .allocation_unit_size = 16 * 1024,
    };

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.flags = SDMMC_HOST_FLAG_1BIT;
    host.max_freq_khz = SDMMC_FREQ_DEFAULT;

    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 1;
    slot.clk = SD_CLK;
    slot.cmd = SD_CMD;
    slot.d0 = SD_D0;

    esp_err_t err = esp_vfs_fat_sdmmc_mount(SDCARD_MOUNT_POINT, &host, &slot, &mount_cfg, &s_card);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "mount failed: %s", esp_err_to_name(err));
        s_card = NULL;
        return err;
    }
    ESP_LOGI(TAG, "mounted %s (%lluMB)", SDCARD_MOUNT_POINT,
             ((uint64_t)s_card->csd.capacity) * s_card->csd.sector_size / (1024 * 1024));
    return ESP_OK;
}

bool sdcard_mounted(void)
{
    return s_card != NULL;
}
