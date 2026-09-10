#include "voice.h"
#include "voice_internal.h"
#include "voice_codec.h"

#include <math.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_heap_caps.h"

static const char *TAG = "voice";

#define MIN_SPEECH_MS   CONFIG_VOICE_MIN_SPEECH_MS
#define TRAIL_SIL_MS    CONFIG_VOICE_TRAILING_SILENCE_MS
#define SIL_DBFS        CONFIG_VOICE_SILENCE_DBFS
#define MIC_GAIN_DB     CONFIG_VOICE_MIC_GAIN_DB
#define SPK_VOL         CONFIG_VOICE_SPEAKER_VOL

typedef enum { PEND_NONE = 0, PEND_MEMO, PEND_ENROLL, PEND_WAKE_RESP } pending_t;

static voice_cb_t s_cb;
static void *s_ctx;
static SemaphoreHandle_t s_trigger;
static volatile pending_t s_pending;
static volatile bool s_active;
static volatile bool s_key;

static bool s_ww_enrolled;
static int  s_ww_count;
static bool s_listener_running;

int16_t *voice_buf;       // PSRAM capture buffer, mono
size_t   voice_total;     // valid samples

void voice_emit(voice_evt_t e, int elapsed_ms, int total_ms, int level,
                int enroll_index, int enroll_total)
{
    if (s_cb) {
        voice_status_t st = {
            .evt = e, .elapsed_ms = elapsed_ms, .total_ms = total_ms,
            .level = level, .enroll_index = enroll_index, .enroll_total = enroll_total,
        };
        s_cb(&st, s_ctx);
    }
}

bool voice_key_taken(void)
{
    if (s_key) {
        s_key = false;
        return true;
    }
    return false;
}

float voice_frame_dbfs(const int16_t *p, size_t n)
{
    double acc = 0;
    for (size_t i = 0; i < n; i++) {
        acc += (double)p[i] * p[i];
    }
    double rms = sqrt(acc / n);
    if (rms < 1.0) {
        return -96.0f;
    }
    return (float)(20.0 * log10(rms / 32768.0));
}

int voice_level_from_dbfs(float dbfs)
{
    int v = (int)((dbfs + 50.0f) * 2.0f);
    if (v < 0) v = 0;
    if (v > 100) v = 100;
    return v;
}

static bool mic_stream_held_elsewhere(void)
{
#if CONFIG_VOICE_WAKENET_ENABLE
    return true;
#else
    return s_listener_running;
#endif
}

// ---------------------------------------------------------------- memo flow ---

static void do_record(void)
{
    voice_emit(VOICE_EVT_REC_START, 0, 0, 0, 0, 0);
    if (voice_codec_rec_open(MIC_GAIN_DB) != ESP_OK) {
        voice_emit(VOICE_EVT_REC_DONE, 0, 0, 0, 0, 0);
        return;
    }

    voice_total = 0;
    int silence_ms = 0, speech_ms = 0, smooth = 0, tick_acc = 0;
    voice_key_taken();

    while (voice_total + VOICE_FRAME_SAMPLES <= VOICE_MAX_SAMPLES) {
        size_t got = 0;
        if (voice_codec_rec_read(&voice_buf[voice_total], VOICE_FRAME_SAMPLES, &got) != ESP_OK) {
            break;
        }
        float dbfs = voice_frame_dbfs(&voice_buf[voice_total], got);
        voice_total += got;

        if (dbfs > SIL_DBFS) { speech_ms += VOICE_FRAME_MS; silence_ms = 0; }
        else                 { silence_ms += VOICE_FRAME_MS; }

        smooth += (voice_level_from_dbfs(dbfs) - smooth) / 3;
        tick_acc += VOICE_FRAME_MS;
        if (tick_acc >= 180) {
            tick_acc = 0;
            voice_emit(VOICE_EVT_REC_TICK, (int)(voice_total * 1000 / VOICE_RATE), 0, smooth, 0, 0);
        }

        if (voice_key_taken()) {
            ESP_LOGI(TAG, "recording stopped by key");
            break;
        }
        if (speech_ms >= MIN_SPEECH_MS && silence_ms >= TRAIL_SIL_MS) {
            ESP_LOGI(TAG, "recording stopped on silence");
            break;
        }
    }

    if (!mic_stream_held_elsewhere()) {
        voice_codec_rec_close();
    }
    int total_ms = (int)(voice_total * 1000 / VOICE_RATE);
    ESP_LOGI(TAG, "recorded %d ms (%u samples)", total_ms, (unsigned)voice_total);
    voice_emit(VOICE_EVT_REC_DONE, total_ms, total_ms, 0, 0, 0);
}

static void do_playback(void)
{
    int total_ms = (int)(voice_total * 1000 / VOICE_RATE);
    if (voice_total == 0) {
        voice_emit(VOICE_EVT_PLAY_DONE, 0, total_ms, 0, 0, 0);
        return;
    }
    voice_emit(VOICE_EVT_PLAY_START, 0, total_ms, 0, 0, 0);
    if (voice_codec_play_open(SPK_VOL) != ESP_OK) {
        voice_emit(VOICE_EVT_PLAY_DONE, 0, total_ms, 0, 0, 0);
        return;
    }
    voice_key_taken();

    const size_t CHUNK = VOICE_RATE / 10;
    size_t pos = 0;
    while (pos < voice_total) {
        size_t n = voice_total - pos;
        if (n > CHUNK) n = CHUNK;
        if (voice_codec_play_write(&voice_buf[pos], n) != ESP_OK) {
            break;
        }
        pos += n;
        voice_emit(VOICE_EVT_PLAY_TICK, (int)(pos * 1000 / VOICE_RATE), total_ms, 0, 0, 0);
        if (voice_key_taken()) {
            ESP_LOGI(TAG, "playback stopped by key");
            break;
        }
    }
    voice_codec_play_close();
    voice_emit(VOICE_EVT_PLAY_DONE, (int)(pos * 1000 / VOICE_RATE), total_ms, 0, 0, 0);
}

// -------------------------------------------------------------------- task ---

static void refresh_enrolled(void)
{
    s_ww_enrolled = voice_enroll_scan(&s_ww_count);
#if !CONFIG_VOICE_WAKENET_ENABLE
    // The DIY always-on listener; skipped when esp-sr WakeNet owns the mic.
    if (s_ww_enrolled && !s_listener_running) {
        s_listener_running = true;
        voice_wakeword_listener_start();
    } else if (!s_ww_enrolled && !mic_stream_held_elsewhere()) {
        voice_codec_rec_close();
    }
#endif
}

static void session_task(void *arg)
{
    (void)arg;
    for (;;) {
        xSemaphoreTake(s_trigger, portMAX_DELAY);
        pending_t what = s_pending;
        s_pending = PEND_NONE;
        s_active = true;
        s_key = false;
        bool wait_key = true;

        if (what == PEND_ENROLL) {
            voice_emit(VOICE_EVT_ENROLL_START, 0, 0, 0, 0, CONFIG_VOICE_ENROLL_SAMPLES);
            voice_enroll_run();
            refresh_enrolled();
        } else if (what == PEND_WAKE_RESP) {
            voice_emit(VOICE_EVT_WAKE_RESPONSE, 0, 0, 0, 0, 0);
            do_playback();
            vTaskDelay(pdMS_TO_TICKS(1200));   // let "Here we go!" linger
            wait_key = false;                  // auto-return
        } else {
            voice_emit(VOICE_EVT_WAKE, 0, 0, 0, 0, 0);
            do_record();
            do_playback();
        }

        while (wait_key && !voice_key_taken()) {
            vTaskDelay(pdMS_TO_TICKS(50));
        }
        s_active = false;
        voice_emit(VOICE_EVT_IDLE, 0, 0, 0, 0, 0);
    }
}

void voice_start(voice_cb_t cb, void *ctx)
{
    s_cb = cb;
    s_ctx = ctx;

    if (voice_codec_init(VOICE_RATE) != ESP_OK) {
        ESP_LOGE(TAG, "codec init failed; voice disabled");
        return;
    }

    voice_buf = heap_caps_malloc(VOICE_MAX_SAMPLES * sizeof(int16_t),
                                 MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!voice_buf) {
        ESP_LOGE(TAG, "no PSRAM for %u-sample buffer", (unsigned)VOICE_MAX_SAMPLES);
        return;
    }

    s_trigger = xSemaphoreCreateBinary();
    xTaskCreatePinnedToCore(session_task, "voice", 6144, NULL, 5, NULL, 1);
    ESP_LOGI(TAG, "ready (%d Hz, up to %d ms)", VOICE_RATE, CONFIG_VOICE_MAX_RECORD_MS);

    refresh_enrolled();
    ESP_LOGI(TAG, "wake word: %s (%d samples)", s_ww_enrolled ? "enrolled" : "not enrolled", s_ww_count);

#if CONFIG_VOICE_WAKENET_ENABLE
    void voice_wakenet_start(void);
    voice_wakenet_start();
#endif
}

static void kick(pending_t what)
{
    if (!s_active && s_trigger) {
        s_pending = what;
        xSemaphoreGive(s_trigger);
    }
}

void voice_trigger(void)      { kick(PEND_MEMO); }
void voice_enroll_start(void) { kick(PEND_ENROLL); }
void voice_notify_wake(void)  { voice_wake_response(NULL, 0); }

void voice_wake_response(const int16_t *pcm, size_t n)
{
    if (s_active || !s_trigger || !voice_buf) {
        return;
    }
    if (pcm && n > 0) {
        if (n > VOICE_MAX_SAMPLES) {
            n = VOICE_MAX_SAMPLES;
        }
        memcpy(voice_buf, pcm, n * sizeof(int16_t));
        voice_total = n;
    } else {
        voice_total = voice_enroll_load_clip(1, voice_buf, VOICE_MAX_SAMPLES);
    }
    kick(PEND_WAKE_RESP);
}

void voice_key(void)
{
    if (s_active) {
        s_key = true;
    }
}

bool voice_session_active(void)   { return s_active; }
bool voice_wakeword_enrolled(void) { return s_ww_enrolled; }
int  voice_wakeword_count(void)    { return s_ww_count; }
int  voice_sample_rate(void)       { return VOICE_RATE; }

const int16_t *voice_pcm(size_t *samples)
{
    if (samples) {
        *samples = voice_total;
    }
    return voice_buf;
}
