#include "ui_internal.h"
#include "mic_icon.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "app_voice.h"
#include "voice.h"

#define WAVE_W  360
#define WAVE_H  64
#define WAVE_COLS 120

static lv_obj_t *s_icon;
static lv_obj_t *s_state;
static lv_obj_t *s_elapsed;
static lv_obj_t *s_bar;
static lv_obj_t *s_wave;

static uint8_t s_levels[WAVE_COLS];   // rolling recording levels, 0..100
static int s_lv_head;
static voice_evt_t s_prev_evt = VOICE_EVT_IDLE;

lv_obj_t *page_voice_create(lv_obj_t *parent)
{
    s_icon = mic_icon_create(parent, 92);
    lv_obj_align(s_icon, LV_ALIGN_TOP_MID, 0, 16);

    s_state = ui_label(parent, &lv_font_montserrat_28, "Pronto");
    lv_obj_align(s_state, LV_ALIGN_TOP_MID, 0, 118);

    s_elapsed = ui_label(parent, &lv_font_montserrat_20, "0:00");
    lv_obj_align(s_elapsed, LV_ALIGN_TOP_MID, 0, 156);

    s_bar = lv_bar_create(parent);
    lv_obj_set_size(s_bar, 320, 12);
    lv_obj_align(s_bar, LV_ALIGN_TOP_MID, 0, 190);
    lv_obj_set_style_bg_color(s_bar, lv_color_white(), 0);
    lv_obj_set_style_border_color(s_bar, lv_color_black(), 0);
    lv_obj_set_style_border_width(s_bar, 2, 0);
    lv_obj_set_style_bg_color(s_bar, lv_color_black(), LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_bar, 0, 0);
    lv_obj_set_style_radius(s_bar, 0, LV_PART_INDICATOR);
    lv_bar_set_range(s_bar, 0, 100);
    lv_bar_set_value(s_bar, 0, LV_ANIM_OFF);

    uint8_t *wbuf = heap_caps_malloc((size_t)WAVE_W * WAVE_H * 2 + 1024,
                                     MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (wbuf) {
        s_wave = lv_canvas_create(parent);
        lv_canvas_set_buffer(s_wave, wbuf, WAVE_W, WAVE_H, LV_COLOR_FORMAT_RGB565);
        lv_canvas_fill_bg(s_wave, lv_color_white(), LV_OPA_COVER);
        lv_obj_align(s_wave, LV_ALIGN_TOP_MID, 0, 216);
    }

    return parent;
}

static void wave_clear(void)
{
    if (s_wave) {
        lv_canvas_fill_bg(s_wave, lv_color_white(), LV_OPA_COVER);
    }
}

static void wave_draw_columns(const uint8_t *heights01_100, int cols, int cursor_col)
{
    if (!s_wave) {
        return;
    }
    lv_canvas_fill_bg(s_wave, lv_color_white(), LV_OPA_COVER);
    lv_layer_t layer;
    lv_canvas_init_layer(s_wave, &layer);

    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = lv_color_black();
    d.bg_opa = LV_OPA_COVER;
    d.border_width = 0;

    int bw = WAVE_W / cols;
    int mid = WAVE_H / 2;
    for (int i = 0; i < cols; i++) {
        int h = heights01_100[i] * (WAVE_H - 4) / 100;
        if (h < 2) h = 2;
        lv_area_t a = { i * bw, mid - h / 2, i * bw + bw - 1, mid + h / 2 };
        lv_draw_rect(&layer, &d, &a);
    }
    if (cursor_col >= 0 && cursor_col < cols) {
        lv_area_t a = { cursor_col * bw, 0, cursor_col * bw + 1, WAVE_H - 1 };
        lv_draw_rect(&layer, &d, &a);
    }
    lv_canvas_finish_layer(s_wave, &layer);
}

static void draw_recording_wave(void)
{
    uint8_t ordered[WAVE_COLS];
    for (int i = 0; i < WAVE_COLS; i++) {
        ordered[i] = s_levels[(s_lv_head + i) % WAVE_COLS];
    }
    wave_draw_columns(ordered, WAVE_COLS, -1);
}

static void draw_envelope(int play_ms, int total_ms)
{
    size_t n = 0;
    const int16_t *pcm = voice_pcm(&n);
    if (!pcm || n == 0) {
        wave_clear();
        return;
    }
    uint8_t cols[WAVE_COLS];
    size_t per = n / WAVE_COLS;
    if (per == 0) per = 1;
    for (int c = 0; c < WAVE_COLS; c++) {
        size_t start = (size_t)c * per;
        int peak = 0;
        for (size_t i = start; i < start + per && i < n; i++) {
            int v = pcm[i] < 0 ? -pcm[i] : pcm[i];
            if (v > peak) peak = v;
        }
        cols[c] = (uint8_t)(peak * 100 / 32768);
    }
    int cursor = (total_ms > 0) ? (play_ms * WAVE_COLS / total_ms) : -1;
    wave_draw_columns(cols, WAVE_COLS, cursor);
}

void page_voice_update(void)
{
    voice_status_t st;
    if (!app_voice_status(&st)) {
        return;
    }

    // Reset the rolling wave at the start of a new recording.
    if (st.evt == VOICE_EVT_REC_START && s_prev_evt != VOICE_EVT_REC_START) {
        memset(s_levels, 0, sizeof(s_levels));
        s_lv_head = 0;
    }
    if (st.evt == VOICE_EVT_REC_TICK) {
        s_levels[s_lv_head] = (uint8_t)st.level;
        s_lv_head = (s_lv_head + 1) % WAVE_COLS;
    }
    s_prev_evt = st.evt;

    char buf[48];
    int max_ms = CONFIG_VOICE_MAX_RECORD_MS;

    switch (st.evt) {
    case VOICE_EVT_ENROLL_START:
    case VOICE_EVT_ENROLL_PROMPT:
        mic_icon_set(s_icon, 1);
        snprintf(buf, sizeof(buf), "Campione %d / %d", st.enroll_index ? st.enroll_index : 1, st.enroll_total);
        lv_label_set_text(s_elapsed, buf);
        lv_label_set_text(s_state, "Preparati a dire \"Ehi Ciccio\"");
        lv_bar_set_value(s_bar, 0, LV_ANIM_OFF);
        wave_clear();
        break;

    case VOICE_EVT_ENROLL_REC:
        lv_label_set_text(s_state, "Di' \"Ehi Ciccio\"");
        mic_icon_set(s_icon, 1);
        snprintf(buf, sizeof(buf), "Campione %d / %d", st.enroll_index, st.enroll_total);
        lv_label_set_text(s_elapsed, buf);
        s_levels[s_lv_head] = (uint8_t)st.level;
        s_lv_head = (s_lv_head + 1) % WAVE_COLS;
        lv_bar_set_value(s_bar, st.level, LV_ANIM_OFF);
        draw_recording_wave();
        break;

    case VOICE_EVT_ENROLL_SAVED:
        lv_label_set_text(s_state, LV_SYMBOL_OK " Registrato");
        memset(s_levels, 0, sizeof(s_levels));
        s_lv_head = 0;
        break;

    case VOICE_EVT_ENROLL_DONE:
        lv_label_set_text(s_state, "Fatto \xE2\x80\x94 premi il tasto");
        mic_icon_set(s_icon, 0);
        snprintf(buf, sizeof(buf), "%d / %d campioni salvati", st.enroll_index, st.enroll_total);
        lv_label_set_text(s_elapsed, buf);
        lv_bar_set_value(s_bar, 100, LV_ANIM_OFF);
        break;

    case VOICE_EVT_ENROLL_FAIL:
        lv_label_set_text(s_state, "Serve la scheda SD");
        mic_icon_set(s_icon, 0);
        lv_label_set_text(s_elapsed, "premi il tasto");
        lv_bar_set_value(s_bar, 0, LV_ANIM_OFF);
        break;

    case VOICE_EVT_WAKE:
    case VOICE_EVT_REC_START:
    case VOICE_EVT_REC_TICK:
        lv_label_set_text(s_state, "In ascolto\xE2\x80\xA6");
        mic_icon_set(s_icon, 1);
        snprintf(buf, sizeof(buf), "%d:%02d", st.elapsed_ms / 60000, (st.elapsed_ms / 1000) % 60);
        lv_label_set_text(s_elapsed, buf);
        lv_bar_set_value(s_bar, st.elapsed_ms * 100 / (max_ms ? max_ms : 1), LV_ANIM_OFF);
        draw_recording_wave();
        break;

    case VOICE_EVT_REC_DONE:
        lv_label_set_text(s_state, "\xE2\x80\xA6");
        draw_envelope(0, st.total_ms);
        break;

    case VOICE_EVT_PLAY_START:
    case VOICE_EVT_PLAY_TICK:
        lv_label_set_text(s_state, "Riproduzione\xE2\x80\xA6");
        mic_icon_set(s_icon, 2);
        snprintf(buf, sizeof(buf), "%d:%02d / %d:%02d",
                 st.elapsed_ms / 60000, (st.elapsed_ms / 1000) % 60,
                 st.total_ms / 60000, (st.total_ms / 1000) % 60);
        lv_label_set_text(s_elapsed, buf);
        lv_bar_set_value(s_bar, st.total_ms ? st.elapsed_ms * 100 / st.total_ms : 0, LV_ANIM_OFF);
        draw_envelope(st.elapsed_ms, st.total_ms);
        break;

    case VOICE_EVT_PLAY_DONE:
        lv_label_set_text(s_state, "Fatto \xE2\x80\x94 premi il tasto");
        mic_icon_set(s_icon, 0);
        snprintf(buf, sizeof(buf), "%d:%02d registrati", st.total_ms / 60000, (st.total_ms / 1000) % 60);
        lv_label_set_text(s_elapsed, buf);
        lv_bar_set_value(s_bar, 100, LV_ANIM_OFF);
        draw_envelope(st.total_ms, st.total_ms);
        break;

    case VOICE_EVT_IDLE:
    default:
        break;
    }
}
