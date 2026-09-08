// Font-free weather glyphs: sun / cloud / rain / snow / fog / storm / ... drawn
// as filled shapes onto an LVGL canvas so they stay crisp on the 1bpp panel.
#pragma once

#include "lvgl.h"

// Square RGB565 canvas (buffer in PSRAM) as a child of `parent`.
lv_obj_t *weather_icon_create(lv_obj_t *parent, int size);

// Draw the icon for a full condition string (e.g. "Rain, Partially cloudy").
// The normalized key is derived via weather_condition_key().
void weather_icon_set(lv_obj_t *canvas, const char *condition_text);
