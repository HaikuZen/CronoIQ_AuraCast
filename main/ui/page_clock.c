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
static lv_obj_t *s_bot_loc;     // outdoor row, line 1: location + bot's report time
static lv_obj_t *s_bot_detail;  // outdoor row, line 2: temp/humidity/wind/rain
static lv_obj_t *s_temp;        // indoor
static lv_obj_t *s_status;
static char s_last_hhmm[6];
static char s_last_outcond[64];

#define BOT_ROW_WIDTH 384  // matches the 7-seg clock's width

static char s_bot_loc_text[96];
static char s_bot_detail_text[96];

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

    s_time = seven_seg_create(parent, 384, 108);
    lv_obj_align(s_time, LV_ALIGN_TOP_MID, 0, 76);
    s_last_hhmm[0] = '\0';

    s_bot_loc = ui_label(parent, &lv_font_montserrat_16, "");
    lv_obj_set_width(s_bot_loc, BOT_ROW_WIDTH);
    lv_label_set_long_mode(s_bot_loc, LV_LABEL_LONG_DOT);   // "..." instead of a hard clip
    lv_obj_set_style_text_align(s_bot_loc, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_bot_loc, LV_ALIGN_TOP_MID, 0, 188);
    lv_obj_add_flag(s_bot_loc, LV_OBJ_FLAG_HIDDEN);

    s_bot_detail = ui_label(parent, &lv_font_montserrat_16, "");
    lv_obj_set_width(s_bot_detail, BOT_ROW_WIDTH);
    lv_label_set_long_mode(s_bot_detail, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(s_bot_detail, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_bot_detail, LV_ALIGN_TOP_MID, 0, 208);
    lv_obj_add_flag(s_bot_detail, LV_OBJ_FLAG_HIDDEN);

    s_bot_loc_text[0] = '\0';
    s_bot_detail_text[0] = '\0';

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

    // Outdoor badge from the weather worker. Zero-initialized so that if
    // app_weather_get() hasn't got a fetch yet (returns false, leaving w
    // untouched), every later check against w.ok/w.cur_* below safely reads
    // as "no data" instead of uninitialized stack garbage.
    weather_data_t w = {0};
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

    // Outdoor rows. When the telegram overlay/provider is active (cur_wind is
    // only ever set by provider_telegram_cml.c -- see weather.h), show the
    // bot's full reply across two lines: location + its own report time, then
    // temp/humidity/wind+direction/today's rain. Otherwise fall back to a
    // plain one-line temp+humidity readout from whatever the primary provider
    // gave us (second line left blank).
    if (w.ok && w.cur_humidity >= 0) {
        char loc_line[96];
        char detail_line[96];
        const char *unit = cfg->units_metric ? "C" : "F";
        if (w.cur_wind >= 0) {
            const char *unit_w = cfg->units_metric ? "km/h" : "mph";
            const char *unit_p = cfg->units_metric ? "mm" : "in";
            snprintf(loc_line, sizeof(loc_line), "%s - %s", w.location_label, w.cur_asof);
            snprintf(detail_line, sizeof(detail_line),
                     "T: %.1f\xC2\xB0%s  H: %d%%  W: %.1f %s da %s  R: %.1f %s",
                     w.cur_temp, unit, w.cur_humidity, w.cur_wind, unit_w,
                     w.cur_wind_dir[0] ? w.cur_wind_dir : "--",
                     w.cur_precip >= 0 ? w.cur_precip : 0.0f, unit_p);
        } else {
            strlcpy(loc_line, w.location_label, sizeof(loc_line));
            snprintf(detail_line, sizeof(detail_line), "%.1f\xC2\xB0%s   %.0f%% RH",
                     w.cur_temp, unit, (float)w.cur_humidity);
        }

        if (strcmp(loc_line, s_bot_loc_text) != 0) {
            strlcpy(s_bot_loc_text, loc_line, sizeof(s_bot_loc_text));
            lv_label_set_text(s_bot_loc, loc_line);
        }
        if (strcmp(detail_line, s_bot_detail_text) != 0) {
            strlcpy(s_bot_detail_text, detail_line, sizeof(s_bot_detail_text));
            lv_label_set_text(s_bot_detail, detail_line);
        }
        if (loc_line[0]) {
            lv_obj_clear_flag(s_bot_loc, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_bot_loc, LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_clear_flag(s_bot_detail, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_bot_loc, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_bot_detail, LV_OBJ_FLAG_HIDDEN);
        s_bot_loc_text[0] = '\0';
        s_bot_detail_text[0] = '\0';
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
