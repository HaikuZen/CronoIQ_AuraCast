#include "ui.h"
#include "ui_internal.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"

#include "lvgl_port.h"

static const char *TAG = "ui";

#define PAGE_COUNT   3    // pages in the KEY cycle
#define SCREEN_MSG   2
#define SCREEN_VOICE 3    // page 4: voice memo / enrollment

typedef enum { OVL_NONE = 0, OVL_VOICE, OVL_WAKE } overlay_t;

static lv_obj_t *s_screens[PAGE_COUNT + 1];
static int s_page;
static int s_saved_page;
static overlay_t s_overlay;
static QueueHandle_t s_evt_q;

lv_obj_t *ui_make_screen(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(scr, lv_color_black(), 0);
    lv_obj_set_style_border_width(scr, 0, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    return scr;
}

lv_obj_t *ui_label(lv_obj_t *parent, const lv_font_t *font, const char *init)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_black(), 0);
    lv_label_set_text(l, init ? init : "");
    return l;
}

static void update_page(int n)
{
    switch (n) {
    case 0: page_clock_update(); break;
    case 1: page_weather_update(); break;
    case SCREEN_MSG: page_message_update(); break;
    case SCREEN_VOICE: page_voice_update(); break;
    }
}

static void update_active(void)
{
    switch (s_overlay) {
    case OVL_VOICE: page_voice_update(); break;
    case OVL_WAKE:  page_message_update(); break;
    default:        update_page(s_page); break;
    }
}

static void show_page(int n)
{
    n = ((n % PAGE_COUNT) + PAGE_COUNT) % PAGE_COUNT;
    s_page = n;
    lv_screen_load(s_screens[n]);
    ESP_LOGI(TAG, "page %d", n + 1);
    lvgl_port_antighost();   // clear ghosting from the previous page
    update_page(n);
}

static void antighost_cb(lv_timer_t *t)
{
    (void)t;
    lvgl_port_antighost();
    update_active();
}

static void tick_cb(lv_timer_t *t)
{
    (void)t;
    update_active();
}

static void meter_cb(lv_timer_t *t)
{
    (void)t;
    if (s_overlay == OVL_VOICE) {
        page_voice_update();
    }
}

static void ui_task(void *arg)
{
    (void)arg;
    for (;;) {
        int cmd;
        if (xQueueReceive(s_evt_q, &cmd, portMAX_DELAY) == pdTRUE) {
            if (s_overlay != OVL_NONE) {
                continue;   // KEY belongs to the voice feature while an overlay is up
            }
            if (lvgl_lock(-1)) {
                show_page(s_page + 1);
                lvgl_unlock();
            }
        }
    }
}

void ui_start(void)
{
    s_evt_q = xQueueCreate(4, sizeof(int));

    lvgl_lock(-1);

    s_screens[0] = ui_make_screen();
    page_clock_create(s_screens[0]);

    s_screens[1] = ui_make_screen();
    page_weather_create(s_screens[1]);

    s_screens[SCREEN_MSG] = ui_make_screen();
    page_message_create(s_screens[SCREEN_MSG]);

    s_screens[SCREEN_VOICE] = ui_make_screen();
    page_voice_create(s_screens[SCREEN_VOICE]);

    show_page(0);
    lv_timer_create(tick_cb, 1000, NULL);
    lv_timer_create(meter_cb, 120, NULL);
    lv_timer_create(antighost_cb, 5 * 60 * 1000, NULL);   // periodic de-ghost

    lvgl_unlock();

    xTaskCreatePinnedToCore(ui_task, "ui", 4096, NULL, 4, NULL, 0);
}

void ui_next_page(void)
{
    if (s_evt_q) {
        int cmd = 1;
        xQueueSend(s_evt_q, &cmd, 0);
    }
}

int ui_current_page(void)
{
    return s_overlay != OVL_NONE ? -1 : s_page;
}

void ui_enter_voice(void)
{
    if (lvgl_lock(-1)) {
        if (s_overlay == OVL_NONE) {
            s_saved_page = s_page;
        }
        s_overlay = OVL_VOICE;
        lv_screen_load(s_screens[SCREEN_VOICE]);
        lvgl_port_antighost();
        page_voice_update();
        lvgl_unlock();
    }
}

void ui_exit_voice(void)
{
    if (lvgl_lock(-1)) {
        s_overlay = OVL_NONE;
        show_page(s_saved_page);
        lvgl_unlock();
    }
}

void ui_enter_wake(void)
{
    if (lvgl_lock(-1)) {
        s_overlay = OVL_WAKE;
        page_message_set_wake(true);
        lv_screen_load(s_screens[SCREEN_MSG]);
        lvgl_port_antighost();
        page_message_update();
        lvgl_unlock();
    }
}

void ui_exit_wake(void)
{
    if (lvgl_lock(-1)) {
        page_message_set_wake(false);
        s_overlay = OVL_NONE;
        show_page(SCREEN_MSG);   // stay on the "Hello!" page
        lvgl_unlock();
    }
}
