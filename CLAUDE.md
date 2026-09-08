# CLAUDE.md

Guidance for working in this repo. Read `README.md` for the product-level picture.

## What this is

ESP-IDF firmware for the **Waveshare ESP32-S3-RLCD-4.2** smart clock: a 3-page
LVGL app (clock / weather / "Hello!") on a 400×300 monochrome reflective LCD.
KEY button (GPIO18) cycles pages. Config comes from `/sdcard/config.json` with
compiled fallbacks.

## Build / flash

```sh
. $IDF_PATH/export.sh          # ESP-IDF >= 5.3; this repo is built against v6.1
idf.py set-target esp32s3      # once
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

On this machine the toolchain is at `/root/.espressif/v6.1/esp-idf`:

```sh
source /root/.espressif/tools/activate_idf_v6.1.sh
export IDF_PATH=/root/.espressif/v6.1/esp-idf
source $IDF_PATH/export.sh
```

There is **no board attached here** — you can compile but not flash or observe
runtime behaviour. Treat display and provider code as "builds, unverified".

## Layout & responsibilities

```
components/board/     Hardware BSP. C, plain esp-idf drivers. No app logic.
components/weather/    HTTPS fetch + JSON parse -> weather_data_t. No UI, no config struct.
main/                  Glue: config, clocks, wifi, worker tasks, LVGL UI.
main/ui/               One file per page. ui.c owns the page manager.
```

Dependency direction: `main` → (`board`, `weather`). `board` and `weather` never
include anything from `main`. `weather` takes a plain `weather_query_t`, not
`app_config_t`, so it stays standalone.

### Key files

| File | Purpose |
|---|---|
| `main/app_main.c` | bring-up order (display → LVGL → I²C/sensors → config → time → workers → UI → Wi-Fi) |
| `main/app_config.c` | `config.json` parse + `DEF_*` compiled defaults at the top |
| `components/board/display_st7305.c` | panel init sequence + 1bpp landscape pixel packing |
| `components/board/lvgl_port.c` | LVGL v9 display, RGB565→B/W threshold flush, handler task, `lvgl_lock/unlock` |
| `components/weather/weather.c` | provider dispatch, `weather_http_get`, condition-text normalisation |

## Conventions

- **Language**: C throughout. 4-space indent, K&R braces, `snake_case`.
- **Logging**: `ESP_LOGx(TAG, ...)`, one `static const char *TAG` per file.
- **Errors**: return `esp_err_t`; `ESP_ERROR_CHECK` only for truly fatal bring-up.
  App/worker code logs and degrades (no SD → defaults, no Wi-Fi → RTC time, failed
  fetch → keep last data).
- **LVGL**: any LVGL call from outside the `lvgl` task must hold `lvgl_lock(-1)` /
  `lvgl_unlock()`. Page `*_update()` functions run inside an `lv_timer` (already on
  the LVGL task) so they don't lock.
- **Shared state**: `app_sensors` uses a `portMUX` critical section; `app_weather`
  uses a mutex. Button callbacks run in the esp_timer task — post to a queue, never
  call LVGL from there (`ui_next_page` does this).
- **Fonts**: only Montserrat sizes enabled in `sdkconfig.defaults` (14/16/20/28/48)
  are linked. Add a `CONFIG_LV_FONT_MONTSERRAT_*` line before using a new size.
  The big clock and the large temperatures do **not** use a font —
  `main/ui/seven_seg.c` draws chunky 7-segment glyphs (`0-9 : - C F °`) as filled
  rects on an LVGL canvas (PSRAM buffer). Weather icons are likewise font-free:
  `main/ui/weather_icon.c` draws sun/cloud/rain/etc. as shapes, keyed off
  `weather_condition_key()`. Regular Montserrat looks thin on this panel; prefer
  these renderers (or a generated bold font) for anything read across the room.
  Both canvases live in PSRAM; a page redraws them only when the underlying value
  changes (see the `s_last_*` guards in the page files).
- **Pins**: defined as `GPIO_NUM_*` macros next to the driver that owns them, not in
  a central header.

## Gotchas

- **cJSON** is not a core component in ESP-IDF v6 — it's the managed dependency
  `espressif/cjson`, referenced in CMake as `espressif__cjson`. Declared in both
  `main/idf_component.yml` and `components/weather/idf_component.yml`.
- **Display packing** (`display_st7305.c`): landscape only, 200 column-groups of
  75 bytes; formula `idx = (x>>1)*75 + (299-y)>>2`, `bit = 7 - (((299-y)&3)<<1 | x&1)`.
  Bit set = white. Ported from the vendor `09_LVGL_V9_Test` demo — if the panel
  shows mirrored/offset, this and the `0x36` MADCTL byte are the first suspects.
- **Flush is whole-frame** (`LV_DISPLAY_RENDER_MODE_FULL`): ~120k pixel writes +
  a 15 KB SPI push per refresh. Fine for a clock; don't add high-FPS animation.
- **Ghosting**: the reflective panel retains a faint after-image. `rlcd_flash()`
  (black→white) clears it; `ui.c` calls it on every page switch and every 5 min
  via `lvgl_port_antighost()`. It blocks the LVGL task ~180 ms and causes a
  visible flash, so don't call it per-frame.
- **Open-Meteo / OpenWeatherMap** need `"lat,lon"` — they don't geocode names.
  Only Visual Crossing accepts a place string.
- **Condition text**: providers store the *full* human string in `cur_cond` /
  `days[].cond` (Open-Meteo gets `weather_wmo_text()`). Icon selection is a
  separate step via `weather_condition_key()` — don't collapse the two.
- **Place name**: `weather_fetch()` reverse-geocodes `"lat,lon"` to "City, CC"
  via BigDataCloud (keyless) unless `weather.city` is set or the provider already
  returned a real name.
- Weather worker waits for the first NTP sync (up to 60 s) before its first fetch
  so forecast-day `mktime` timestamps are correct.

## Common tasks

**Add a weather provider**: new `components/weather/provider_x.c` exporting
`weather_provider_x(const weather_query_t*, weather_data_t*)`; add it to the
dispatch `if` chain and the `SRCS` list in `weather.c` / CMake. Normalise
conditions via `weather_norm_text()` or `weather_norm_wmo()` into the vocabulary
listed in `weather.h`.

**Add a page**: `main/ui/page_x.c` with `page_x_create(parent)` + `page_x_update()`,
declare in `ui_internal.h`, bump `PAGE_COUNT` and the `s_screens`/`switch` blocks
in `ui.c`, add to `main/CMakeLists.txt`.

**Change a default**: `DEF_*` macros at the top of `main/app_config.c`.
