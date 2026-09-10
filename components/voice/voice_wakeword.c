// Always-on "Ehi Ciccio" listener.
//
// Enrollment (voice_enroll.c) captures reference clips to
// /sdcard/wakeword/ehiciccio_{1..N}.wav. This task then runs continuously: it
// endpoints each spoken utterance from the mic and passes it to ww_match().
//
// ww_match() is a STUB. Per project decision the on-device matcher is deferred:
// to make the phrase actually trigger, load the enrolled clips here, compute a
// feature sequence (log-mel / MFCC via espressif/esp-dsp) for each, and DTW-match
// the live utterance against them — or drop in a converted WakeNet model and use
// voice_wakenet.c instead. On a positive match call voice_notify_wake().

#include "voice_internal.h"
#include "voice_codec.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_heap_caps.h"

static const char *TAG = "voice_ww";

#define MIC_GAIN_DB   CONFIG_VOICE_MIC_GAIN_DB
#define SIL_DBFS      CONFIG_VOICE_SILENCE_DBFS
#define UTT_MAX_MS    2500
#define UTT_START_MS  120     // speech needed to open an utterance
#define UTT_END_MS    400     // trailing silence that closes it

// Returns true if `pcm` (mono @ VOICE_RATE) matches the enrolled wake word.
// TODO: implement (see file header). Currently always false.
static bool ww_match(const int16_t *pcm, size_t n)
{
    (void)pcm;
    (void)n;
    return false;
}

static void listener_task(void *arg)
{
    (void)arg;

    const size_t frame = VOICE_FRAME_SAMPLES;
    const size_t cap = (size_t)VOICE_RATE * UTT_MAX_MS / 1000;
    int16_t *utt = heap_caps_malloc(cap * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    int16_t *fr = heap_caps_malloc(frame * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!utt || !fr) {
        ESP_LOGE(TAG, "no PSRAM for listener");
        vTaskDelete(NULL);
        return;
    }

    voice_codec_rec_open(MIC_GAIN_DB);
    ESP_LOGI(TAG, "listening for the wake word");

    bool in_utt = false;
    size_t used = 0;
    int speech_ms = 0, silence_ms = 0;

    for (;;) {
        if (voice_session_active()) {
            in_utt = false;
            used = 0;
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        size_t got = 0;
        if (voice_codec_rec_read(fr, frame, &got) != ESP_OK || got == 0) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        bool voiced = voice_frame_dbfs(fr, got) > SIL_DBFS;

        if (!in_utt) {
            speech_ms = voiced ? speech_ms + VOICE_FRAME_MS : 0;
            if (speech_ms >= UTT_START_MS) {
                in_utt = true;
                used = 0;
                silence_ms = 0;
            }
            continue;
        }

        if (used + got <= cap) {
            memcpy(&utt[used], fr, got * sizeof(int16_t));
            used += got;
        }
        silence_ms = voiced ? 0 : silence_ms + VOICE_FRAME_MS;

        if (silence_ms >= UTT_END_MS || used + got > cap) {
            in_utt = false;
            if (used > (size_t)VOICE_RATE / 5 && ww_match(utt, used)) {
                ESP_LOGI(TAG, "wake word matched (%u ms)", (unsigned)(used * 1000 / VOICE_RATE));
                voice_wake_response(utt, used);   // -> page 3 "Here we go!" + playback
            }
            speech_ms = 0;
        }
    }
}

void voice_wakeword_listener_start(void)
{
    xTaskCreatePinnedToCore(listener_task, "ww_listen", 4096, NULL, 4, NULL, 1);
}
