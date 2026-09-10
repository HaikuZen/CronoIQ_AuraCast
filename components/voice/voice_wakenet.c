// Hands-free wake word via esp-sr AFE + WakeNet.
//
// Compiled ONLY when CONFIG_VOICE_WAKENET_ENABLE=y. UNTESTED on this board — it
// is the integration seam for when an "Ehi Ciccio" WakeNet model exists:
//   1. add a `model` partition (see partitions.csv / README)
//   2. `idf.py add-dependency "espressif/esp-sr"` is already handled by this
//      component's idf_component.yml when the flag is on
//   3. select / drop in the wake word model via `idf.py menuconfig`
//      (ESP Speech Recognition -> WakeNet) and `idf.py erase-flash flash`
//
// Feeds AFE 1-channel 16 kHz mono (down-mixed from the 2-mic ES7210). On
// detection it calls voice_notify_wake(); detection pauses while a session runs.

#include "sdkconfig.h"
#if CONFIG_VOICE_WAKENET_ENABLE

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_heap_caps.h"

#include "esp_afe_sr_models.h"
#include "esp_afe_config.h"
#include "model_path.h"

#include "voice.h"
#include "voice_codec.h"

static const char *TAG = "voice_wn";

void voice_notify_wake(void);   // from voice.c

static const esp_afe_sr_iface_t *s_afe;
static esp_afe_sr_data_t *s_data;
static int s_chunk;             // feed samples per channel

static void feed_task(void *arg)
{
    (void)arg;
    int16_t *buf = heap_caps_malloc(s_chunk * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) {
        vTaskDelete(NULL);
        return;
    }
    for (;;) {
        if (voice_session_active()) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        size_t got = 0;
        if (voice_codec_rec_read(buf, s_chunk, &got) == ESP_OK) {
            s_afe->feed(s_data, buf);
        } else {
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }
}

static void fetch_task(void *arg)
{
    (void)arg;
    for (;;) {
        afe_fetch_result_t *res = s_afe->fetch(s_data);
        if (!res || res->ret_value == -1) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        if (res->wakeup_state == WAKENET_DETECTED && !voice_session_active()) {
            ESP_LOGI(TAG, "wake word detected (idx %d)", res->wake_word_index);
            voice_notify_wake();
        }
    }
}

void voice_wakenet_start(void)
{
    srmodel_list_t *models = esp_srmodel_init("model");
    if (!models) {
        ESP_LOGE(TAG, "no `model` partition / models — WakeNet disabled");
        return;
    }
    char *wn = esp_srmodel_filter(models, ESP_WN_PREFIX, NULL);
    if (!wn) {
        ESP_LOGE(TAG, "no WakeNet model flashed — WakeNet disabled");
        return;
    }
    ESP_LOGI(TAG, "WakeNet model: %s", wn);

    afe_config_t *cfg = afe_config_init("M", models, AFE_TYPE_SR, AFE_MODE_LOW_COST);
    cfg->aec_init = false;
    cfg->se_init = false;
    cfg->vad_init = false;
    cfg->ns_init = false;
    cfg->wakenet_init = true;
    cfg->wakenet_model_name = wn;
    cfg->memory_alloc_mode = AFE_MEMORY_ALLOC_MORE_PSRAM;   // spare internal RAM for TLS

    s_afe = esp_afe_handle_from_config(cfg);
    s_data = s_afe->create_from_config(cfg);
    afe_config_free(cfg);
    if (!s_data) {
        ESP_LOGE(TAG, "AFE create failed");
        return;
    }
    s_chunk = s_afe->get_feed_chunksize(s_data);

    // Keep the mic stream open for continuous feeding.
    voice_codec_rec_open(CONFIG_VOICE_MIC_GAIN_DB);

    xTaskCreatePinnedToCore(feed_task, "wn_feed", 4096, NULL, 5, NULL, 1);
    xTaskCreatePinnedToCore(fetch_task, "wn_fetch", 4096, NULL, 5, NULL, 1);
    ESP_LOGI(TAG, "WakeNet running (chunk=%d)", s_chunk);
}

#endif // CONFIG_VOICE_WAKENET_ENABLE
