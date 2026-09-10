// Font-free microphone glyph on an LVGL canvas (same idea as weather_icon.c).
#pragma once

#include "lvgl.h"

lv_obj_t *mic_icon_create(lv_obj_t *parent, int size);

// style: 0 = idle mic, 1 = recording (filled), 2 = playback (speaker waves)
void mic_icon_set(lv_obj_t *canvas, int style);
