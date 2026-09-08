#include "app_sensors.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "shtc3.h"

static const char *TAG = "app_sensors";

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static float s_temp_c;
static float s_rh;
static bool s_valid;
static int s_offset_dc;

static void sensors_task(void *arg)
{
    (void)arg;
    for (;;) {
        float t, h;
        if (shtc3_read(&t, &h) == ESP_OK) {
            t += s_offset_dc / 10.0f;
            portENTER_CRITICAL(&s_lock);
            s_temp_c = t;
            s_rh = h;
            s_valid = true;
            portEXIT_CRITICAL(&s_lock);
            ESP_LOGD(TAG, "%.1f C  %.0f %%RH", t, h);
        } else {
            ESP_LOGW(TAG, "SHTC3 read failed");
        }
        vTaskDelay(pdMS_TO_TICKS(20000));
    }
}

void app_sensors_start(int offset_dc)
{
    s_offset_dc = offset_dc;
    xTaskCreate(sensors_task, "sensors", 3072, NULL, 3, NULL);
}

bool app_sensors_get(float *temp_c, float *rh_pct)
{
    bool ok;
    portENTER_CRITICAL(&s_lock);
    ok = s_valid;
    if (temp_c) *temp_c = s_temp_c;
    if (rh_pct) *rh_pct = s_rh;
    portEXIT_CRITICAL(&s_lock);
    return ok;
}
