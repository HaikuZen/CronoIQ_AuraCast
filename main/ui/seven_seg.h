// Chunky 7-segment renderer for the big clock digits. No font assets — draws
// filled rounded rectangles onto an LVGL canvas, so it stays crisp and heavy at
// any size on the 1bpp reflective panel.
#pragma once

#include "lvgl.h"

// Creates an RGB565 canvas (buffer in PSRAM) as a child of `parent`.
lv_obj_t *seven_seg_create(lv_obj_t *parent, lv_coord_t w, lv_coord_t h);

// Renders a short string; supported glyphs: '0'-'9', ':', ' '. The layout is
// sized to fill the canvas width/height. Safe to call from the LVGL task.
void seven_seg_set_text(lv_obj_t *canvas, const char *txt);
