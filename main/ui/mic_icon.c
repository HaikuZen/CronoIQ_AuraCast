#include "mic_icon.h"

#include <math.h>

#include "esp_heap_caps.h"
#include "esp_log.h"

static const char *TAG = "mic_icon";

lv_obj_t *mic_icon_create(lv_obj_t *parent, int size)
{
    uint8_t *buf = heap_caps_malloc((size_t)size * size * 2 + 1024,
                                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) {
        ESP_LOGE(TAG, "alloc failed");
        return NULL;
    }
    lv_obj_t *c = lv_canvas_create(parent);
    lv_canvas_set_buffer(c, buf, size, size, LV_COLOR_FORMAT_RGB565);
    lv_canvas_fill_bg(c, lv_color_white(), LV_OPA_COVER);
    return c;
}

static void box(lv_layer_t *l, const lv_draw_rect_dsc_t *d, float x, float y, float w, float h)
{
    if (w <= 0 || h <= 0) return;
    lv_area_t a = { (int)x, (int)y, (int)(x + w - 1), (int)(y + h - 1) };
    lv_draw_rect(l, d, &a);
}

static void stroke(lv_layer_t *l, lv_draw_line_dsc_t *d, float x1, float y1, float x2, float y2, float w)
{
    d->p1.x = x1; d->p1.y = y1; d->p2.x = x2; d->p2.y = y2; d->width = (int)w;
    lv_draw_line(l, d);
}

void mic_icon_set(lv_obj_t *canvas, int style)
{
    lv_draw_buf_t *db = lv_canvas_get_draw_buf(canvas);
    if (!db) return;
    int S = db->header.w;

    lv_canvas_fill_bg(canvas, lv_color_white(), LV_OPA_COVER);
    lv_layer_t layer;
    lv_canvas_init_layer(canvas, &layer);

    lv_draw_rect_dsc_t r;
    lv_draw_rect_dsc_init(&r);
    r.bg_color = lv_color_black();
    r.bg_opa = LV_OPA_COVER;
    r.border_width = 0;

    lv_draw_line_dsc_t ln;
    lv_draw_line_dsc_init(&ln);
    ln.color = lv_color_black();
    ln.opa = LV_OPA_COVER;
    ln.round_start = ln.round_end = 1;

    if (style == 2) {
        // speaker + sound waves
        float cx = S * 0.34f, cy = S * 0.5f;
        box(&layer, &r, cx - S * 0.20f, cy - S * 0.10f, S * 0.14f, S * 0.20f);
        lv_draw_triangle_dsc_t t;
        lv_draw_triangle_dsc_init(&t);
        t.bg_color = lv_color_black();
        t.bg_opa = LV_OPA_COVER;
        t.p[0].x = cx - S * 0.06f; t.p[0].y = cy - S * 0.22f;
        t.p[1].x = cx - S * 0.06f; t.p[1].y = cy + S * 0.22f;
        t.p[2].x = cx + S * 0.14f; t.p[2].y = cy;
        lv_draw_triangle(&layer, &t);
        for (int i = 1; i <= 3; i++) {
            float rr = S * (0.10f * i);
            stroke(&layer, &ln, cx + S * 0.20f, cy - rr, cx + S * 0.20f + rr * 0.5f, cy, S * 0.05f);
            stroke(&layer, &ln, cx + S * 0.20f + rr * 0.5f, cy, cx + S * 0.20f, cy + rr, S * 0.05f);
        }
        lv_canvas_finish_layer(canvas, &layer);
        return;
    }

    // mic capsule + stand + base
    r.radius = LV_RADIUS_CIRCLE;
    float mw = S * 0.30f, mh = S * 0.50f;
    float mx = S * 0.5f - mw / 2, my = S * 0.14f;
    box(&layer, &r, mx, my, mw, mh);

    if (style != 1) {
        // hollow it out for the idle look
        lv_draw_rect_dsc_t hole = r;
        hole.bg_color = lv_color_white();
        float t = S * 0.06f;
        box(&layer, &hole, mx + t, my + t, mw - 2 * t, mh - 2 * t);
    }

    r.radius = 0;
    // cradle arc (approx with two strokes) + stand + base
    stroke(&layer, &ln, S * 0.24f, S * 0.52f, S * 0.32f, S * 0.70f, S * 0.055f);
    stroke(&layer, &ln, S * 0.76f, S * 0.52f, S * 0.68f, S * 0.70f, S * 0.055f);
    stroke(&layer, &ln, S * 0.32f, S * 0.70f, S * 0.68f, S * 0.70f, S * 0.055f);
    stroke(&layer, &ln, S * 0.5f, S * 0.70f, S * 0.5f, S * 0.84f, S * 0.06f);
    stroke(&layer, &ln, S * 0.36f, S * 0.86f, S * 0.64f, S * 0.86f, S * 0.06f);

    lv_canvas_finish_layer(canvas, &layer);
}
