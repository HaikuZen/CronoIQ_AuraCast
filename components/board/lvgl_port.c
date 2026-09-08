#include "lvgl_port.h"
#include "display_st7305.h"

#include <assert.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

static const char *TAG = "lvgl_port";

#define LVGL_TASK_STACK   (8 * 1024)
#define LVGL_TASK_PRIO    4
#define LVGL_MAX_SLEEP_MS 40

static SemaphoreHandle_t s_mutex;

static uint32_t tick_get_cb(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    const uint16_t *p = (const uint16_t *)px_map;
    for (int y = area->y1; y <= area->y2; y++) {
        for (int x = area->x1; x <= area->x2; x++) {
            uint16_t c = *p++;
            uint32_t r = (c >> 11) & 0x1F;
            uint32_t g = (c >> 5) & 0x3F;
            uint32_t b = c & 0x1F;
            // luminance approx, max ~ 31*3 + 63*3 + 31*2 = 344
            uint32_t lum = r * 3 + g * 3 + b * 2;
            rlcd_pixel(x, y, lum > 172);
        }
    }
    rlcd_refresh();
    lv_display_flush_ready(disp);
}

bool lvgl_lock(int timeout_ms)
{
    TickType_t t = (timeout_ms < 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTakeRecursive(s_mutex, t) == pdTRUE;
}

void lvgl_unlock(void)
{
    xSemaphoreGiveRecursive(s_mutex);
}

void lvgl_port_antighost(void)
{
    rlcd_flash();
    lv_obj_t *scr = lv_screen_active();
    if (scr) {
        lv_obj_invalidate(scr);
    }
}

static void lvgl_task(void *arg)
{
    (void)arg;
    for (;;) {
        uint32_t next_ms = LVGL_MAX_SLEEP_MS;
        if (lvgl_lock(-1)) {
            next_ms = lv_timer_handler();
            lvgl_unlock();
        }
        if (next_ms > LVGL_MAX_SLEEP_MS) {
            next_ms = LVGL_MAX_SLEEP_MS;
        }
        vTaskDelay(pdMS_TO_TICKS(next_ms == 0 ? 1 : next_ms));
    }
}

void lvgl_port_init(void)
{
    s_mutex = xSemaphoreCreateRecursiveMutex();
    assert(s_mutex);

    lv_init();
    lv_tick_set_cb(tick_get_cb);

    lv_display_t *disp = lv_display_create(RLCD_WIDTH, RLCD_HEIGHT);
    lv_display_set_flush_cb(disp, flush_cb);

    // Two full-frame RGB565 buffers in PSRAM.
    size_t buf_bytes = (size_t)RLCD_WIDTH * RLCD_HEIGHT * 2;
    void *b1 = heap_caps_malloc(buf_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    void *b2 = heap_caps_malloc(buf_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    assert(b1 && b2);
    lv_display_set_buffers(disp, b1, b2, buf_bytes, LV_DISPLAY_RENDER_MODE_FULL);

    xTaskCreatePinnedToCore(lvgl_task, "lvgl", LVGL_TASK_STACK, NULL, LVGL_TASK_PRIO, NULL, 0);
    ESP_LOGI(TAG, "LVGL %d.%d ready", LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR);
}
