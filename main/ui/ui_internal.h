#pragma once

#include "lvgl.h"

// Shared look helpers for the monochrome reflective panel.
lv_obj_t *ui_make_screen(void);
lv_obj_t *ui_label(lv_obj_t *parent, const lv_font_t *font, const char *init);

// Each page: build once onto `parent`, then update on a timer.
lv_obj_t *page_clock_create(lv_obj_t *parent);
void      page_clock_update(void);

lv_obj_t *page_weather_create(lv_obj_t *parent);
void      page_weather_update(void);

lv_obj_t *page_message_create(lv_obj_t *parent);
void      page_message_update(void);
void      page_message_set_wake(bool on);   // "Here we go!" wake-response mode

lv_obj_t *page_voice_create(lv_obj_t *parent);
void      page_voice_update(void);
