#include "seven_seg.h"

#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"

static const char *TAG = "seven_seg";

// Segment bits: a=0 b=1 c=2 d=3 e=4 f=5 g=6
static const uint8_t kDigitSeg[10] = {
    0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F,
};
#define SEG_C   0x39
#define SEG_F   0x71
#define SEG_DASH 0x40

// Glyph tokens after UTF-8 decoding.
enum { G_NONE, G_DIGIT, G_COLON, G_SPACE, G_DEGREE, G_C, G_F, G_DASH };

lv_obj_t *seven_seg_create(lv_obj_t *parent, lv_coord_t w, lv_coord_t h)
{
    size_t bytes = (size_t)w * h * 2 + 1024;
    uint8_t *buf = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) {
        ESP_LOGE(TAG, "canvas alloc failed (%u bytes)", (unsigned)bytes);
        return NULL;
    }
    lv_obj_t *canvas = lv_canvas_create(parent);
    lv_canvas_set_buffer(canvas, buf, w, h, LV_COLOR_FORMAT_RGB565);
    lv_canvas_fill_bg(canvas, lv_color_white(), LV_OPA_COVER);
    return canvas;
}

// Decode one glyph, advancing *pp past its bytes. Returns G_NONE at end.
static int next_glyph(const char **pp, int *digit)
{
    const unsigned char *p = (const unsigned char *)*pp;
    if (*p == '\0') {
        return G_NONE;
    }
    if (p[0] == 0xC2 && p[1] == 0xB0) {          // UTF-8 degree sign
        *pp += 2;
        return G_DEGREE;
    }
    *pp += 1;
    if (*p >= '0' && *p <= '9') { *digit = *p - '0'; return G_DIGIT; }
    switch (*p) {
    case ':': return G_COLON;
    case ' ': return G_SPACE;
    case 'C': case 'c': return G_C;
    case 'F': case 'f': return G_F;
    case '-': return G_DASH;
    default:  return G_SPACE;
    }
}

static void fill_rect(lv_layer_t *layer, const lv_draw_rect_dsc_t *dsc,
                      int x, int y, int w, int h)
{
    if (w <= 0 || h <= 0) {
        return;
    }
    lv_area_t a = { x, y, x + w - 1, y + h - 1 };
    lv_draw_rect(layer, dsc, &a);
}

// One 7-segment glyph inside box (x, y, dw, dh) with stroke thickness t.
static void draw_seg(lv_layer_t *layer, const lv_draw_rect_dsc_t *dsc,
                     int x, int y, int dw, int dh, int t, uint8_t seg)
{
    int e = t * 3 / 10;
    int half = dh / 2;
    int vlen = half - e - e / 2;

    if (seg & (1 << 0)) fill_rect(layer, dsc, x + e,      y,                dw - 2 * e, t);
    if (seg & (1 << 1)) fill_rect(layer, dsc, x + dw - t, y + e,            t,          vlen);
    if (seg & (1 << 2)) fill_rect(layer, dsc, x + dw - t, y + half + e / 2, t,          vlen);
    if (seg & (1 << 3)) fill_rect(layer, dsc, x + e,      y + dh - t,       dw - 2 * e, t);
    if (seg & (1 << 4)) fill_rect(layer, dsc, x,          y + half + e / 2, t,          vlen);
    if (seg & (1 << 5)) fill_rect(layer, dsc, x,          y + e,            t,          vlen);
    if (seg & (1 << 6)) fill_rect(layer, dsc, x + e,      y + (dh - t) / 2, dw - 2 * e, t);
}

void seven_seg_set_text(lv_obj_t *canvas, const char *txt)
{
    if (!canvas || !txt) {
        return;
    }
    lv_draw_buf_t *db = lv_canvas_get_draw_buf(canvas);
    if (!db) {
        return;
    }
    int cw = db->header.w;
    int ch = db->header.h;

    // Vertical margin as a fraction of canvas height — larger = smaller digits.
    int my = ch * 7 / 100;
    int dh = ch - 2 * my;
    int t = dh * 18 / 100;           // heavy stroke
    int dw = dh * 58 / 100;
    int colon_w = t * 3 / 2;
    int deg_w = dw * 44 / 100;
    int gap = t;

    // Measure for horizontal centering.
    int total = 0;
    const char *p = txt;
    int d;
    for (int g; (g = next_glyph(&p, &d)) != G_NONE; ) {
        switch (g) {
        case G_COLON:  total += colon_w + gap; break;
        case G_SPACE:  total += dw / 2 + gap;  break;
        case G_DEGREE: total += deg_w + gap;   break;
        default:       total += dw + gap;      break;
        }
    }
    if (total > 0) {
        total -= gap;
    }

    int x = (cw - total) / 2;
    if (x < 0) {
        x = 0;
    }
    int y = my;

    lv_canvas_fill_bg(canvas, lv_color_white(), LV_OPA_COVER);

    lv_layer_t layer;
    lv_canvas_init_layer(canvas, &layer);

    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color = lv_color_black();
    dsc.bg_opa = LV_OPA_COVER;
    dsc.border_width = 0;
    dsc.radius = t / 4;

    p = txt;
    for (int g; (g = next_glyph(&p, &d)) != G_NONE; ) {
        switch (g) {
        case G_DIGIT:
            draw_seg(&layer, &dsc, x, y, dw, dh, t, kDigitSeg[d]);
            x += dw + gap;
            break;
        case G_C:
            draw_seg(&layer, &dsc, x, y, dw, dh, t, SEG_C);
            x += dw + gap;
            break;
        case G_F:
            draw_seg(&layer, &dsc, x, y, dw, dh, t, SEG_F);
            x += dw + gap;
            break;
        case G_DASH:
            draw_seg(&layer, &dsc, x, y, dw, dh, t, SEG_DASH);
            x += dw + gap;
            break;
        case G_COLON: {
            int sq = t;
            int cx = x + (colon_w - sq) / 2;
            fill_rect(&layer, &dsc, cx, y + dh * 28 / 100, sq, sq);
            fill_rect(&layer, &dsc, cx, y + dh * 62 / 100, sq, sq);
            x += colon_w + gap;
            break;
        }
        case G_DEGREE: {
            int rt = t * 3 / 5;
            fill_rect(&layer, &dsc, x,              y,              deg_w, rt);
            fill_rect(&layer, &dsc, x,              y + deg_w - rt, deg_w, rt);
            fill_rect(&layer, &dsc, x,              y,              rt,    deg_w);
            fill_rect(&layer, &dsc, x + deg_w - rt, y,              rt,    deg_w);
            x += deg_w + gap;
            break;
        }
        case G_SPACE:
        default:
            x += dw / 2 + gap;
            break;
        }
    }

    lv_canvas_finish_layer(canvas, &layer);
}
