#include "ui_internal.h"
#include "weather_icon.h"
#include "seven_seg.h"

#include <time.h>
#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "app_weather.h"
#include "wifi_sta.h"
#include "weather.h"

static lv_obj_t *s_loc;
static lv_obj_t *s_cur_icon;
static lv_obj_t *s_cur_temp;      // 7-segment canvas
static lv_obj_t *s_cur_cond;      // full condition text (wraps)
static lv_obj_t *s_cur_extra;     // feels-like / humidity
static lv_obj_t *s_div;
static lv_obj_t *s_day_icon[WEATHER_MAX_DAYS];
static lv_obj_t *s_day_lbl[WEATHER_MAX_DAYS];
static lv_obj_t *s_foot;

#define ROW_Y0   162
#define ROW_H    22
#define DAY_ICON 22

lv_obj_t *page_weather_create(lv_obj_t *parent)
{
    s_loc = ui_label(parent, &lv_font_montserrat_20, "Weather");
    lv_obj_set_width(s_loc, 392);
    lv_label_set_long_mode(s_loc, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(s_loc, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_loc, LV_ALIGN_TOP_MID, 0, 4);

    s_cur_icon = weather_icon_create(parent, 84);
    lv_obj_align(s_cur_icon, LV_ALIGN_TOP_LEFT, 12, 30);

    s_cur_temp = seven_seg_create(parent, 210, 82);
    lv_obj_align(s_cur_temp, LV_ALIGN_TOP_LEFT, 104, 28);

    s_cur_cond = ui_label(parent, &lv_font_montserrat_16, "");
    lv_obj_set_width(s_cur_cond, 290);
    lv_label_set_long_mode(s_cur_cond, LV_LABEL_LONG_WRAP);
    lv_obj_align(s_cur_cond, LV_ALIGN_TOP_LEFT, 104, 108);

    s_cur_extra = ui_label(parent, &lv_font_montserrat_14, "");
    lv_obj_align(s_cur_extra, LV_ALIGN_TOP_LEFT, 12, 136);

    s_div = lv_obj_create(parent);
    lv_obj_set_size(s_div, 376, 2);
    lv_obj_set_style_bg_color(s_div, lv_color_black(), 0);
    lv_obj_set_style_border_width(s_div, 0, 0);
    lv_obj_align(s_div, LV_ALIGN_TOP_MID, 0, ROW_Y0 - 10);

    for (int i = 0; i < WEATHER_MAX_DAYS; i++) {
        s_day_icon[i] = weather_icon_create(parent, DAY_ICON);
        lv_obj_align(s_day_icon[i], LV_ALIGN_TOP_LEFT, 12, ROW_Y0 + i * ROW_H);
        lv_obj_add_flag(s_day_icon[i], LV_OBJ_FLAG_HIDDEN);

        s_day_lbl[i] = ui_label(parent, &lv_font_montserrat_16, "");
        lv_obj_set_width(s_day_lbl[i], 344);
        lv_label_set_long_mode(s_day_lbl[i], LV_LABEL_LONG_DOT);
        lv_obj_align(s_day_lbl[i], LV_ALIGN_TOP_LEFT, 42, ROW_Y0 + i * ROW_H + 1);
        lv_obj_add_flag(s_day_lbl[i], LV_OBJ_FLAG_HIDDEN);
    }

    s_foot = ui_label(parent, &lv_font_montserrat_14, "");
    lv_obj_align(s_foot, LV_ALIGN_BOTTOM_MID, 0, -4);

    return parent;
}

static void hide_all_days(void)
{
    for (int i = 0; i < WEATHER_MAX_DAYS; i++) {
        lv_obj_add_flag(s_day_icon[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_day_lbl[i], LV_OBJ_FLAG_HIDDEN);
    }
}

void page_weather_update(void)
{
    const app_config_t *cfg = app_config_get();
    const char *unit = cfg->units_metric ? "C" : "F";

    weather_data_t w;
    char buf[128];

    if (!app_weather_get(&w) || !w.ok) {
        lv_label_set_text(s_loc, "Weather");
        weather_icon_set(s_cur_icon, "unknown");
        seven_seg_set_text(s_cur_temp, "--");
        lv_label_set_text(s_cur_cond, wifi_sta_connected() ? "loading forecast..." : "offline");
        lv_label_set_text(s_cur_extra, "");
        hide_all_days();
        lv_label_set_text(s_foot, "");
        return;
    }

    lv_label_set_text(s_loc, w.location_label[0] ? w.location_label : cfg->location);

    weather_icon_set(s_cur_icon, w.cur_cond);

    snprintf(buf, sizeof(buf), "%.0f\xC2\xB0%s", w.cur_temp, unit);
    seven_seg_set_text(s_cur_temp, buf);

    lv_label_set_text(s_cur_cond, w.cur_cond);

    if (w.cur_humidity >= 0) {
        snprintf(buf, sizeof(buf), "feels %.0f\xC2\xB0%s   humidity %d%%",
                 w.cur_feels, unit, w.cur_humidity);
    } else {
        snprintf(buf, sizeof(buf), "feels %.0f\xC2\xB0%s", w.cur_feels, unit);
    }
    lv_label_set_text(s_cur_extra, buf);

    for (int i = 0; i < WEATHER_MAX_DAYS; i++) {
        if (i >= w.day_count) {
            lv_obj_add_flag(s_day_icon[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(s_day_lbl[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        struct tm tm;
        localtime_r(&w.days[i].date, &tm);
        char wd[10];
        strftime(wd, sizeof(wd), (i == 0) ? "Today" : "%a", &tm);

        if (w.days[i].pop >= 0) {
            snprintf(buf, sizeof(buf), "%-4s %.0f\xC2\xB0/%.0f\xC2\xB0  %s  %d%%",
                     wd, w.days[i].tmin, w.days[i].tmax, w.days[i].cond, w.days[i].pop);
        } else {
            snprintf(buf, sizeof(buf), "%-4s %.0f\xC2\xB0/%.0f\xC2\xB0  %s",
                     wd, w.days[i].tmin, w.days[i].tmax, w.days[i].cond);
        }
        lv_label_set_text(s_day_lbl[i], buf);
        weather_icon_set(s_day_icon[i], w.days[i].cond);
        lv_obj_clear_flag(s_day_icon[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_day_lbl[i], LV_OBJ_FLAG_HIDDEN);
    }

    struct tm tm;
    localtime_r(&w.updated, &tm);
    char hhmm[8];
    strftime(hhmm, sizeof(hhmm), "%H:%M", &tm);
    bool stale = (time(NULL) - w.updated) > 90 * 60;
    snprintf(buf, sizeof(buf), "%s   |   updated %s%s",
             w.provider, hhmm, stale ? " (stale)" : "");
    lv_label_set_text(s_foot, buf);
}
