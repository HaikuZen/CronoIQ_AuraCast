#include "voice_internal.h"
#include "voice_codec.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "voice_enroll";

#define ENROLL_N        CONFIG_VOICE_ENROLL_SAMPLES
#define ENROLL_MAX_MS   CONFIG_VOICE_ENROLL_MAX_MS
#define ENROLL_SIL_MS   CONFIG_VOICE_ENROLL_SILENCE_MS
#define MIC_GAIN_DB     CONFIG_VOICE_MIC_GAIN_DB
#define SIL_DBFS        CONFIG_VOICE_SILENCE_DBFS

static void clip_path(char *buf, size_t cap, int i)
{
    snprintf(buf, cap, VOICE_WW_DIR "/ehiciccio_%d.wav", i);
}

bool voice_enroll_scan(int *count_out)
{
    int n = 0;
    for (int i = 1; i <= ENROLL_N; i++) {
        char p[64];
        clip_path(p, sizeof(p), i);
        struct stat st;
        if (stat(p, &st) == 0 && st.st_size > 44) {
            n++;
        }
    }
    if (count_out) {
        *count_out = n;
    }
    struct stat st;
    return (n >= ENROLL_N) && (stat(VOICE_WW_MARKER, &st) == 0);
}

size_t voice_enroll_load_clip(int index, int16_t *dst, size_t cap)
{
    char p[64];
    clip_path(p, sizeof(p), index);
    FILE *f = fopen(p, "rb");
    if (!f) {
        return 0;
    }
    fseek(f, 44, SEEK_SET);   // skip WAV header
    size_t n = fread(dst, sizeof(int16_t), cap, f);
    fclose(f);
    return n;
}

static int write_wav(const char *path, const int16_t *pcm, size_t samples, int rate)
{
    FILE *f = fopen(path, "wb");
    if (!f) {
        ESP_LOGE(TAG, "open %s failed", path);
        return -1;
    }
    uint32_t data_bytes = samples * 2;
    uint32_t byte_rate = rate * 2;
    uint8_t h[44];
    memcpy(h, "RIFF", 4);
    uint32_t riff = 36 + data_bytes;
    h[4] = riff; h[5] = riff >> 8; h[6] = riff >> 16; h[7] = riff >> 24;
    memcpy(h + 8, "WAVEfmt ", 8);
    h[16] = 16; h[17] = h[18] = h[19] = 0;           // fmt chunk size
    h[20] = 1;  h[21] = 0;                            // PCM
    h[22] = 1;  h[23] = 0;                            // mono
    h[24] = rate; h[25] = rate >> 8; h[26] = rate >> 16; h[27] = rate >> 24;
    h[28] = byte_rate; h[29] = byte_rate >> 8; h[30] = byte_rate >> 16; h[31] = byte_rate >> 24;
    h[32] = 2; h[33] = 0;                             // block align
    h[34] = 16; h[35] = 0;                            // bits
    memcpy(h + 36, "data", 4);
    h[40] = data_bytes; h[41] = data_bytes >> 8; h[42] = data_bytes >> 16; h[43] = data_bytes >> 24;

    size_t ok = fwrite(h, 1, 44, f) == 44;
    ok &= fwrite(pcm, 1, data_bytes, f) == data_bytes;
    fclose(f);
    return ok ? 0 : -1;
}

// Record one utterance into voice_buf. Returns samples captured, 0 on abort.
static size_t record_one(int index)
{
    voice_emit(VOICE_EVT_ENROLL_REC, 0, 0, 0, index, ENROLL_N);

    size_t n = 0;
    int silence_ms = 0, speech_ms = 0, smooth = 0, tick = 0;
    const size_t cap = (size_t)VOICE_RATE * ENROLL_MAX_MS / 1000;
    voice_key_taken();

    while (n + VOICE_FRAME_SAMPLES <= cap && n + VOICE_FRAME_SAMPLES <= VOICE_MAX_SAMPLES) {
        size_t got = 0;
        if (voice_codec_rec_read(&voice_buf[n], VOICE_FRAME_SAMPLES, &got) != ESP_OK) {
            break;
        }
        float dbfs = voice_frame_dbfs(&voice_buf[n], got);
        n += got;

        if (dbfs > SIL_DBFS) { speech_ms += VOICE_FRAME_MS; silence_ms = 0; }
        else                 { silence_ms += VOICE_FRAME_MS; }

        smooth += (voice_level_from_dbfs(dbfs) - smooth) / 3;
        tick += VOICE_FRAME_MS;
        if (tick >= 120) {
            tick = 0;
            voice_emit(VOICE_EVT_ENROLL_REC, (int)(n * 1000 / VOICE_RATE), 0, smooth, index, ENROLL_N);
        }

        if (voice_key_taken()) {
            return 0;
        }
        if (speech_ms >= 300 && silence_ms >= ENROLL_SIL_MS) {
            break;
        }
    }
    return n;
}

void voice_enroll_run(void)
{
    struct stat st;
    if (stat("/sdcard", &st) != 0) {
        ESP_LOGW(TAG, "no SD card");
        voice_emit(VOICE_EVT_ENROLL_FAIL, 0, 0, 0, 0, ENROLL_N);
        return;
    }
    mkdir(VOICE_WW_DIR, 0777);
    remove(VOICE_WW_MARKER);

    if (voice_codec_rec_open(MIC_GAIN_DB) != ESP_OK) {
        voice_emit(VOICE_EVT_ENROLL_FAIL, 0, 0, 0, 0, ENROLL_N);
        return;
    }

    int saved = 0;
    for (int i = 1; i <= ENROLL_N; i++) {
        voice_emit(VOICE_EVT_ENROLL_PROMPT, 0, 0, 0, i, ENROLL_N);
        for (int t = 0; t < 12; t++) {           // ~1.2 s "get ready"
            vTaskDelay(pdMS_TO_TICKS(100));
            if (voice_key_taken()) {
                goto done;
            }
        }

        size_t n = record_one(i);
        if (n == 0) {
            ESP_LOGW(TAG, "sample %d aborted", i);
            goto done;
        }
        char p[64];
        clip_path(p, sizeof(p), i);
        if (write_wav(p, voice_buf, n, VOICE_RATE) == 0) {
            saved++;
            ESP_LOGI(TAG, "sample %d: %u ms -> %s", i, (unsigned)(n * 1000 / VOICE_RATE), p);
            voice_emit(VOICE_EVT_ENROLL_SAVED, (int)(n * 1000 / VOICE_RATE), 0, 0, i, ENROLL_N);
        } else {
            voice_emit(VOICE_EVT_ENROLL_FAIL, 0, 0, 0, i, ENROLL_N);
            goto done;
        }
    }

    if (saved >= ENROLL_N) {
        FILE *m = fopen(VOICE_WW_MARKER, "w");
        if (m) {
            fprintf(m, "ehiciccio %d\n", saved);
            fclose(m);
        }
    }

done:
    // Leave the mic stream open — refresh_enrolled() starts the always-on
    // listener right after this and it needs the stream.
    ESP_LOGI(TAG, "enrollment finished: %d/%d saved", saved, ENROLL_N);
    voice_emit(VOICE_EVT_ENROLL_DONE, 0, 0, 0, saved, ENROLL_N);
}
