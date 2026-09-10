#include "app_voice.h"
#include "ui/ui.h"

#include "freertos/FreeRTOS.h"
#include "esp_log.h"

static const char *TAG = "app_voice";

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static voice_status_t s_last;
static bool s_have;
static bool s_wake_overlay;   // page 3 "Here we go!" vs. page 4 voice overlay

static void on_evt(const voice_status_t *st, void *ctx)
{
    (void)ctx;
    portENTER_CRITICAL(&s_lock);
    s_last = *st;
    s_have = true;
    portEXIT_CRITICAL(&s_lock);

    switch (st->evt) {
    case VOICE_EVT_WAKE_RESPONSE:
        ESP_LOGI(TAG, "wake word -> page 3 \"Here we go!\"");
        s_wake_overlay = true;
        ui_enter_wake();
        break;
    case VOICE_EVT_WAKE:
    case VOICE_EVT_ENROLL_START:
        ESP_LOGI(TAG, "voice session -> page 4");
        s_wake_overlay = false;
        ui_enter_voice();
        break;
    case VOICE_EVT_IDLE:
        ESP_LOGI(TAG, "session end");
        if (s_wake_overlay) {
            s_wake_overlay = false;
            ui_exit_wake();
        } else {
            ui_exit_voice();
        }
        break;
    default:
        break;
    }
}

void app_voice_start(void)
{
    voice_start(on_evt, NULL);
}

void app_voice_enroll_start(void)
{
    voice_enroll_start();
}

bool app_voice_ww_enrolled(void)
{
    return voice_wakeword_enrolled();
}

int app_voice_ww_count(void)
{
    return voice_wakeword_count();
}

bool app_voice_status(voice_status_t *out)
{
    bool h;
    portENTER_CRITICAL(&s_lock);
    h = s_have;
    if (out) {
        *out = s_last;
    }
    portEXIT_CRITICAL(&s_lock);
    return h;
}

bool app_voice_on_key(board_btn_t btn, board_btn_evt_t evt)
{
    (void)evt;
    if (btn != BOARD_BTN_KEY) {
        return false;
    }
    if (voice_session_active()) {
        voice_key();          // stop rec / stop play / next enrollment step / leave
        return true;
    }
    return false;            // KEY long-press is dispatched per-page by app_main
}
