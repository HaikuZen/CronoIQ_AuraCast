#include "wifi_sta.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"

static const char *TAG = "wifi_sta";

static wifi_got_ip_cb_t s_got_ip_cb;
static volatile bool s_connected;
static SemaphoreHandle_t s_reconnect_sig;
static int s_retry;

static void reconnect_task(void *arg)
{
    (void)arg;
    for (;;) {
        xSemaphoreTake(s_reconnect_sig, portMAX_DELAY);
        int delay_ms = (s_retry < 5) ? 2000 : (s_retry < 15 ? 10000 : 30000);
        s_retry++;
        ESP_LOGW(TAG, "reconnect attempt #%d in %d ms", s_retry, delay_ms);
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
        esp_wifi_connect();
    }
}

static void on_wifi(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
        xSemaphoreGive(s_reconnect_sig);
    }
}

static void on_ip(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = data;
        ESP_LOGI(TAG, "got IP " IPSTR, IP2STR(&e->ip_info.ip));
        s_connected = true;
        s_retry = 0;
        if (s_got_ip_cb) {
            s_got_ip_cb();
        }
    }
}

void wifi_sta_start(const char *ssid, const char *pass, wifi_got_ip_cb_t on_got_ip)
{
    s_got_ip_cb = on_got_ip;
    s_reconnect_sig = xSemaphoreCreateBinary();
    xTaskCreate(reconnect_task, "wifi_recon", 3072, NULL, 4, NULL);

    esp_err_t nvs = nvs_flash_init();
    if (nvs == ESP_ERR_NVS_NO_FREE_PAGES || nvs == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_ip, NULL, NULL));

    wifi_config_t wc = { 0 };
    strlcpy((char *)wc.sta.ssid, ssid, sizeof(wc.sta.ssid));
    strlcpy((char *)wc.sta.password, pass, sizeof(wc.sta.password));
    wc.sta.threshold.authmode = WIFI_AUTH_OPEN;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "connecting to \"%s\"", ssid);
}

bool wifi_sta_connected(void)
{
    return s_connected;
}

int wifi_sta_rssi(void)
{
    wifi_ap_record_t ap;
    if (s_connected && esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        return ap.rssi;
    }
    return 0;
}
