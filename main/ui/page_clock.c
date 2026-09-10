#include "ui_internal.h"
#include "seven_seg.h"
#include "weather_icon.h"

#include <time.h>
#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "app_sensors.h"
#include "app_weather.h"
#include "wifi_sta.h"
#include "time_sync.h"
#include "battery.h"
#include "weather.h"

static lv_obj_t *s_weekday;
static lv_obj_t *s_date;
static lv_obj_t *s_time;        // 7-segment canvas
static lv_obj_t *s_out_icon;    // outdoor weather badge
static lv_obj_t *s_out_temp;
static lv_obj_t *s_temp;        // indoor
static lv_obj_t *s_status;
static char s_last_hhmm[6];
static char s_last_outcond[64];

lv_obj_t *page_clock_create(lv_obj_t *parent)
{
    s_weekday = ui_label(parent, &lv_font_montserrat_28, "—");
    lv_obj_align(s_weekday, LV_ALIGN_TOP_MID, 0, 8);

    s_date = ui_label(parent, &lv_font_montserrat_20, "—");
    lv_obj_align(s_date, LV_ALIGN_TOP_MID, 0, 44);

    s_out_icon = weather_icon_create(parent, 46);
    lv_obj_align(s_out_icon, LV_ALIGN_TOP_RIGHT, -8, 6);
    lv_obj_add_flag(s_out_icon, LV_OBJ_FLAG_HIDDEN);

    s_out_temp = ui_label(parent, &lv_font_montserrat_20, "");
    lv_obj_set_style_text_align(s_out_temp, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(s_out_temp, LV_ALIGN_TOP_RIGHT, -10, 54);
    lv_obj_add_flag(s_out_temp, LV_OBJ_FLAG_HIDDEN);

    s_time = seven_seg_create(parent, 384, 140);
    lv_obj_align(s_time, LV_ALIGN_TOP_MID, 0, 76);
    s_last_hhmm[0] = '\0';

    s_temp = ui_label(parent, &lv_font_montserrat_28, LV_SYMBOL_HOME "  -- \xC2\xB0");
    lv_obj_align(s_temp, LV_ALIGN_BOTTOM_MID, 0, -34);

    s_status = ui_label(parent, &lv_font_montserrat_16, "");
    lv_obj_align(s_status, LV_ALIGN_BOTTOM_MID, 0, -8);

    return parent;
}

static const char *battery_symbol(int pct)
{
    if (pct >= 80) return LV_SYMBOL_BATTERY_FULL;
    if (pct >= 55) return LV_SYMBOL_BATTERY_3;
    if (pct >= 35) return LV_SYMBOL_BATTERY_2;
    if (pct >= 15) return LV_SYMBOL_BATTERY_1;
    return LV_SYMBOL_BATTERY_EMPTY;
}

void page_clock_update(void)
{
    const app_config_t *cfg = app_config_get();

    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);

    char buf[96];

    char hhmm[6];
    strftime(hhmm, sizeof(hhmm), "%H:%M", &tm);
    if (s_time && strcmp(hhmm, s_last_hhmm) != 0) {
        seven_seg_set_text(s_time, hhmm);
        strcpy(s_last_hhmm, hhmm);
    }

    strftime(buf, sizeof(buf), "%A", &tm);
    lv_label_set_text(s_weekday, buf);
    strftime(buf, sizeof(buf), "%d %B %Y", &tm);
    lv_label_set_text(s_date, buf);

    // Indoor temperature / humidity from the SHTC3.
    float t_c, rh;
    if (app_sensors_get(&t_c, &rh)) {
        if (cfg->units_metric) {
            snprintf(buf, sizeof(buf), LV_SYMBOL_HOME "  %.1f \xC2\xB0""C   %.0f%%", t_c, rh);
        } else {
            snprintf(buf, sizeof(buf), LV_SYMBOL_HOME "  %.1f \xC2\xB0""F   %.0f%%",
                     t_c * 9.0f / 5.0f + 32.0f, rh);
        }
        lv_label_set_text(s_temp, buf);
    }

    // Outdoor badge from the weather worker.
    weather_data_t w;
    if (app_weather_get(&w) && w.ok) {
        if (strcmp(w.cur_cond, s_last_outcond) != 0) {
            weather_icon_set(s_out_icon, w.cur_cond);
            strlcpy(s_last_outcond, w.cur_cond, sizeof(s_last_outcond));
        }
        snprintf(buf, sizeof(buf), "%.0f\xC2\xB0%s", w.cur_temp, cfg->units_metric ? "C" : "F");
        lv_label_set_text(s_out_temp, buf);
        lv_obj_clear_flag(s_out_icon, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_out_temp, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_out_icon, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_out_temp, LV_OBJ_FLAG_HIDDEN);
    }

    // Status line: wifi + time source + battery.
    const char *wifi_sym = wifi_sta_connected() ? LV_SYMBOL_WIFI : LV_SYMBOL_WARNING;
    const char *src = time_sync_resyncing() ? LV_SYMBOL_REFRESH " sync"
                      : (time_sync_ntp_done() ? "NTP" : "RTC");
    int pct = battery_percent();
    if (battery_voltage() > 2.5f) {
        snprintf(buf, sizeof(buf), "%s   %s   %s %d%%", wifi_sym, src, battery_symbol(pct), pct);
    } else {
        snprintf(buf, sizeof(buf), "%s   %s", wifi_sym, src);
    }
    lv_label_set_text(s_status, buf);
}
