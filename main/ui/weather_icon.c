#include "weather_icon.h"

#include <math.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "weather.h"

static const char *TAG = "weather_icon";

lv_obj_t *weather_icon_create(lv_obj_t *parent, int size)
{
    size_t bytes = (size_t)size * size * 2 + 1024;
    uint8_t *buf = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) {
        ESP_LOGE(TAG, "canvas alloc failed");
        return NULL;
    }
    lv_obj_t *c = lv_canvas_create(parent);
    lv_canvas_set_buffer(c, buf, size, size, LV_COLOR_FORMAT_RGB565);
    lv_canvas_fill_bg(c, lv_color_white(), LV_OPA_COVER);
    return c;
}

typedef struct {
    lv_layer_t *layer;
    lv_draw_rect_dsc_t rect;
    lv_draw_line_dsc_t line;
    int s;   // canvas size
} ctx_t;

static void disc(ctx_t *g, float cx, float cy, float r)
{
    g->rect.radius = LV_RADIUS_CIRCLE;
    lv_area_t a = { (int)(cx - r), (int)(cy - r), (int)(cx + r), (int)(cy + r) };
    lv_draw_rect(g->layer, &g->rect, &a);
}

static void box(ctx_t *g, float x, float y, float w, float h, float rad)
{
    g->rect.radius = (int)rad;
    lv_area_t a = { (int)x, (int)y, (int)(x + w), (int)(y + h) };
    lv_draw_rect(g->layer, &g->rect, &a);
}

static void stroke(ctx_t *g, float x1, float y1, float x2, float y2, float w)
{
    g->line.p1.x = x1; g->line.p1.y = y1;
    g->line.p2.x = x2; g->line.p2.y = y2;
    g->line.width = (int)w;
    lv_draw_line(g->layer, &g->line);
}

static void cloud(ctx_t *g, float cx, float cy, float half_w)
{
    float S = g->s;
    disc(g, cx - half_w * 0.72f, cy, S * 0.19f);
    disc(g, cx + half_w * 0.78f, cy, S * 0.21f);
    disc(g, cx - half_w * 0.05f, cy - S * 0.14f, S * 0.25f);
    box(g, cx - half_w, cy - S * 0.02f, 2 * half_w, S * 0.22f, S * 0.11f);
}

static void sun(ctx_t *g, float cx, float cy, float r, bool rays)
{
    disc(g, cx, cy, r);
    if (!rays) {
        return;
    }
    for (int k = 0; k < 8; k++) {
        float a = k * (float)M_PI / 4.0f;
        float r1 = r * 1.35f, r2 = r * 1.95f;
        stroke(g, cx + cosf(a) * r1, cy + sinf(a) * r1,
               cx + cosf(a) * r2, cy + sinf(a) * r2, g->s * 0.055f);
    }
}

static void raindrops(ctx_t *g, float y0, float y1)
{
    float S = g->s;
    for (int k = 0; k < 3; k++) {
        float x = S * (0.32f + 0.18f * k);
        stroke(g, x, S * y0, x - S * 0.04f, S * y1, S * 0.05f);
    }
}

static void flake(ctx_t *g, float cx, float cy, float r)
{
    for (int k = 0; k < 3; k++) {
        float a = k * (float)M_PI / 3.0f;
        stroke(g, cx - cosf(a) * r, cy - sinf(a) * r,
               cx + cosf(a) * r, cy + sinf(a) * r, g->s * 0.045f);
    }
}

void weather_icon_set(lv_obj_t *canvas, const char *condition_text)
{
    if (!canvas) {
        return;
    }
    lv_draw_buf_t *db = lv_canvas_get_draw_buf(canvas);
    if (!db) {
        return;
    }
    int S = db->header.w;
    const char *key = weather_condition_key(condition_text);

    lv_canvas_fill_bg(canvas, lv_color_white(), LV_OPA_COVER);

    ctx_t g = { .s = S };
    lv_layer_t layer;
    lv_canvas_init_layer(canvas, &layer);
    g.layer = &layer;

    lv_draw_rect_dsc_init(&g.rect);
    g.rect.bg_color = lv_color_black();
    g.rect.bg_opa = LV_OPA_COVER;
    g.rect.border_width = 0;

    lv_draw_line_dsc_init(&g.line);
    g.line.color = lv_color_black();
    g.line.opa = LV_OPA_COVER;
    g.line.round_start = 1;
    g.line.round_end = 1;

    float cx = S * 0.5f;

    if (strcmp(key, "clear") == 0) {
        sun(&g, cx, S * 0.5f, S * 0.22f, true);
    } else if (strcmp(key, "partly") == 0) {
        sun(&g, S * 0.36f, S * 0.36f, S * 0.16f, true);
        cloud(&g, S * 0.56f, S * 0.62f, S * 0.30f);
    } else if (strcmp(key, "fog") == 0) {
        cloud(&g, cx, S * 0.40f, S * 0.32f);
        for (int k = 0; k < 3; k++) {
            float y = S * (0.64f + 0.13f * k);
            float inset = (k == 1) ? S * 0.26f : S * 0.16f;
            stroke(&g, inset, y, S - inset, y, S * 0.06f);
        }
    } else if (strcmp(key, "drizzle") == 0) {
        cloud(&g, cx, S * 0.42f, S * 0.32f);
        raindrops(&g, 0.64f, 0.78f);
    } else if (strcmp(key, "rain") == 0) {
        cloud(&g, cx, S * 0.40f, S * 0.32f);
        raindrops(&g, 0.60f, 0.88f);
    } else if (strcmp(key, "sleet") == 0) {
        cloud(&g, cx, S * 0.40f, S * 0.32f);
        stroke(&g, S * 0.36f, S * 0.62f, S * 0.32f, S * 0.86f, S * 0.05f);
        flake(&g, S * 0.64f, S * 0.76f, S * 0.09f);
    } else if (strcmp(key, "snow") == 0) {
        cloud(&g, cx, S * 0.40f, S * 0.32f);
        flake(&g, S * 0.34f, S * 0.74f, S * 0.10f);
        flake(&g, S * 0.66f, S * 0.74f, S * 0.10f);
    } else if (strcmp(key, "storm") == 0) {
        cloud(&g, cx, S * 0.38f, S * 0.32f);
        stroke(&g, S * 0.54f, S * 0.54f, S * 0.40f, S * 0.74f, S * 0.055f);
        stroke(&g, S * 0.40f, S * 0.74f, S * 0.56f, S * 0.74f, S * 0.055f);
        stroke(&g, S * 0.56f, S * 0.74f, S * 0.44f, S * 0.96f, S * 0.055f);
    } else if (strcmp(key, "wind") == 0) {
        stroke(&g, S * 0.16f, S * 0.34f, S * 0.74f, S * 0.34f, S * 0.075f);
        stroke(&g, S * 0.16f, S * 0.52f, S * 0.82f, S * 0.52f, S * 0.075f);
        stroke(&g, S * 0.16f, S * 0.70f, S * 0.64f, S * 0.70f, S * 0.075f);
    } else {
        // cloudy / unknown
        cloud(&g, cx, S * 0.5f, S * 0.36f);
    }

    lv_canvas_finish_layer(canvas, &layer);
}
