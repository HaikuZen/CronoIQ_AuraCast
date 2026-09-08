#include "ui.h"
#include "ui_internal.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"

#include "lvgl_port.h"

static const char *TAG = "ui";

#define PAGE_COUNT 3

static lv_obj_t *s_screens[PAGE_COUNT];
static int s_page;
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
    case 2: page_message_update(); break;
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
    update_page(s_page);
}

static void tick_cb(lv_timer_t *t)
{
    (void)t;
    update_page(s_page);
}

static void ui_task(void *arg)
{
    (void)arg;
    for (;;) {
        int cmd;
        if (xQueueReceive(s_evt_q, &cmd, portMAX_DELAY) == pdTRUE) {
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

    s_screens[2] = ui_make_screen();
    page_message_create(s_screens[2]);

    show_page(0);
    lv_timer_create(tick_cb, 1000, NULL);
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
