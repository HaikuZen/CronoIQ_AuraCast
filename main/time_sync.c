#include "time_sync.h"

#include <string.h>
#include <time.h>
#include <sys/time.h>

#include "esp_log.h"
#include "esp_sntp.h"

#include "pcf85063.h"

static const char *TAG = "time_sync";

static char s_ntp_server[64] = "pool.ntp.org";
static volatile bool s_ntp_done;
static bool s_sntp_started;

static void write_system_time_to_rtc(void)
{
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    rtc_time_t rt = {
        .year = tm.tm_year + 1900,
        .month = tm.tm_mon + 1,
        .day = tm.tm_mday,
        .hour = tm.tm_hour,
        .minute = tm.tm_min,
        .second = tm.tm_sec,
        .weekday = tm.tm_wday,
    };
    if (pcf85063_set(&rt) == ESP_OK) {
        ESP_LOGI(TAG, "RTC updated from NTP: %04d-%02d-%02d %02d:%02d:%02d",
                 rt.year, rt.month, rt.day, rt.hour, rt.minute, rt.second);
    }
}

static void on_sync(struct timeval *tv)
{
    (void)tv;
    s_ntp_done = true;
    write_system_time_to_rtc();
}

void time_sync_init(const char *tz, const char *ntp_server)
{
    if (tz && *tz) {
        setenv("TZ", tz, 1);
        tzset();
    }
    if (ntp_server && *ntp_server) {
        strlcpy(s_ntp_server, ntp_server, sizeof(s_ntp_server));
    }

    rtc_time_t rt;
    if (pcf85063_get(&rt) == ESP_OK && !pcf85063_time_invalid()) {
        struct tm tm = {
            .tm_year = rt.year - 1900,
            .tm_mon = rt.month - 1,
            .tm_mday = rt.day,
            .tm_hour = rt.hour,
            .tm_min = rt.minute,
            .tm_sec = rt.second,
            .tm_isdst = -1,
        };
        time_t t = mktime(&tm);
        struct timeval now = { .tv_sec = t };
        settimeofday(&now, NULL);
        ESP_LOGI(TAG, "system clock seeded from RTC: %04d-%02d-%02d %02d:%02d:%02d",
                 rt.year, rt.month, rt.day, rt.hour, rt.minute, rt.second);
    } else {
        ESP_LOGW(TAG, "RTC time not valid; waiting for NTP");
    }
}

void time_sync_start_sntp(void)
{
    if (s_sntp_started) {
        return;
    }
    s_sntp_started = true;
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, s_ntp_server);
    sntp_set_sync_interval(6 * 3600 * 1000);   // 6 h
    sntp_set_time_sync_notification_cb(on_sync);
    esp_sntp_init();
    ESP_LOGI(TAG, "SNTP started (%s)", s_ntp_server);
}

bool time_sync_ntp_done(void)
{
    return s_ntp_done;
}
