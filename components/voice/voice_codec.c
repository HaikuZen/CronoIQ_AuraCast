#include "voice_codec.h"

#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_codec_dev.h"
#include "codec_board.h"
#include "codec_init.h"

static const char *TAG = "voice_codec";

#define CH 2                       // ES7210/ES8311 run 2-channel here
#define TMP_FRAMES 512             // scratch size in stereo frames

static esp_codec_dev_handle_t s_play;
static esp_codec_dev_handle_t s_rec;
static int s_rate = 16000;
static bool s_rec_open;
static SemaphoreHandle_t s_rec_mtx;
static int16_t *s_tmp;             // interleaved stereo scratch (CH * TMP_FRAMES)

int voice_codec_channels(void)
{
    return CH;
}

esp_err_t voice_codec_init(int sample_rate)
{
    if (s_rec || s_play) {
        return ESP_OK;
    }
    s_rate = sample_rate;

    set_codec_board_type("S3_RLCD_4_2");

    codec_init_cfg_t cfg = {
        .in_mode = CODEC_I2S_MODE_TDM,
        .out_mode = CODEC_I2S_MODE_TDM,
        .in_use_tdm = false,
        .reuse_dev = false,
    };
    int rc = init_codec(&cfg);
    if (rc != 0) {
        ESP_LOGE(TAG, "init_codec failed: %d", rc);
        return ESP_FAIL;
    }
    s_play = get_playback_handle();
    s_rec = get_record_handle();
    if (!s_play || !s_rec) {
        ESP_LOGE(TAG, "codec handles missing (play=%p rec=%p)", s_play, s_rec);
        return ESP_FAIL;
    }

    s_tmp = heap_caps_malloc(CH * TMP_FRAMES * sizeof(int16_t), MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    if (!s_tmp) {
        return ESP_ERR_NO_MEM;
    }
    s_rec_mtx = xSemaphoreCreateMutex();
    ESP_LOGI(TAG, "codec ready @ %d Hz", s_rate);
    return ESP_OK;
}

static esp_codec_dev_sample_info_t fs_info(void)
{
    esp_codec_dev_sample_info_t fs = {
        .sample_rate = s_rate,
        .channel = CH,
        .bits_per_sample = 16,
    };
    return fs;
}

esp_err_t voice_codec_rec_open(int mic_gain_db)
{
    if (s_rec_open) {
        return ESP_OK;
    }
    esp_codec_dev_sample_info_t fs = fs_info();
    int rc = esp_codec_dev_open(s_rec, &fs);
    if (rc != 0) {
        ESP_LOGE(TAG, "rec open: %d", rc);
        return ESP_FAIL;
    }
    esp_codec_dev_set_in_gain(s_rec, (float)mic_gain_db);
    s_rec_open = true;
    return ESP_OK;
}

esp_err_t voice_codec_rec_read_raw(int16_t *interleaved, size_t frames)
{
    xSemaphoreTake(s_rec_mtx, portMAX_DELAY);
    int rc = esp_codec_dev_read(s_rec, interleaved, (int)(frames * CH * sizeof(int16_t)));
    xSemaphoreGive(s_rec_mtx);
    return rc == 0 ? ESP_OK : ESP_FAIL;
}

esp_err_t voice_codec_rec_read(int16_t *mono, size_t samples, size_t *got)
{
    xSemaphoreTake(s_rec_mtx, portMAX_DELAY);
    size_t done = 0;
    while (done < samples) {
        size_t chunk = samples - done;
        if (chunk > TMP_FRAMES) {
            chunk = TMP_FRAMES;
        }
        int rc = esp_codec_dev_read(s_rec, s_tmp, (int)(chunk * CH * sizeof(int16_t)));
        if (rc != 0) {
            if (got) {
                *got = done;
            }
            xSemaphoreGive(s_rec_mtx);
            return ESP_FAIL;
        }
        for (size_t i = 0; i < chunk; i++) {
            int32_t l = s_tmp[i * CH];
            int32_t r = s_tmp[i * CH + 1];
            mono[done + i] = (int16_t)((l + r) / 2);
        }
        done += chunk;
    }
    if (got) {
        *got = done;
    }
    xSemaphoreGive(s_rec_mtx);
    return ESP_OK;
}

void voice_codec_rec_close(void)
{
    if (s_rec_open) {
        esp_codec_dev_close(s_rec);
        s_rec_open = false;
    }
}

esp_err_t voice_codec_play_open(int speaker_vol)
{
    esp_codec_dev_sample_info_t fs = fs_info();
    int rc = esp_codec_dev_open(s_play, &fs);
    if (rc != 0) {
        ESP_LOGE(TAG, "play open: %d", rc);
        return ESP_FAIL;
    }
    esp_codec_dev_set_out_vol(s_play, speaker_vol);
    return ESP_OK;
}

esp_err_t voice_codec_play_write(const int16_t *mono, size_t samples)
{
    size_t done = 0;
    while (done < samples) {
        size_t chunk = samples - done;
        if (chunk > TMP_FRAMES) {
            chunk = TMP_FRAMES;
        }
        for (size_t i = 0; i < chunk; i++) {
            s_tmp[i * CH] = s_tmp[i * CH + 1] = mono[done + i];
        }
        int rc = esp_codec_dev_write(s_play, s_tmp, (int)(chunk * CH * sizeof(int16_t)));
        if (rc != 0) {
            return ESP_FAIL;
        }
        done += chunk;
    }
    return ESP_OK;
}

void voice_codec_play_close(void)
{
    esp_codec_dev_close(s_play);
}
