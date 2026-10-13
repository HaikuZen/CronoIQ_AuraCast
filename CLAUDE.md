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
components/board/       Hardware BSP. C, plain esp-idf drivers. No app logic.
components/weather/      HTTPS fetch + JSON parse -> weather_data_t. No UI, no config struct.
components/codec_board/  Vendored espressif/codec_board 0.5.5, trimmed + patched (see below).
components/voice/        Mic capture, silence detection, playback, WakeNet seam. voice.h is the API.
main/                    Glue: config, clocks, wifi, worker tasks, LVGL UI.
main/ui/                 One file per page. ui.c owns the page manager.
```

Dependency direction: `main` → (`board`, `weather`, `voice`). `board`, `weather`,
`voice` never include anything from `main`. `weather` takes a plain
`weather_query_t`; `voice` emits `voice_status_t` through a callback — both stay
standalone. `voice` → `codec_board`.

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
- **Telegram (`@CML_robot`) current-conditions overlay**: `provider_telegram_cml.c`
  does NOT talk to Telegram at all — `@CML_robot` only answers a real user
  account's `/realtime` + locality (as two separate messages) in a private
  chat, and ESP32 firmware has no realistic way to speak Telegram's real
  client protocol (MTProto: encrypted handshake, custom TL binary
  serialization, persistent login session — nothing like a REST API). So this
  provider is just another `weather_http_get()` JSON call, pointed at
  `telegram.bridge_url` — a small off-device script
  (`tools/telegram_cml_bridge.py`, Telethon + aiohttp) that holds the actual
  Telegram login (via `my.telegram.org` `api_id`/`api_hash`) and exposes
  `GET /realtime?locality=...` → `{"ok":true,"text":"<raw reply>"}`. The reply
  text itself is still parsed with `strstr`/`strtof` (fixed Italian format,
  no cJSON for that part) — only the transport changed, not the parsing.
  `day_count` is always 0 (no forecast from this source).
  `app_weather.c:apply_telegram_overlay()` is the real "switch" — it layers
  `cur_temp`/`cur_feels`/`cur_humidity`/`cur_cond`/`location_label` from this
  source onto whatever the primary `weather.provider` returned each fetch, iff
  `telegram.enabled` and both `bridge_url`/`locality` are set; it no-ops if
  `weather.provider` is already `"telegram"` (then `weather_fetch()` alone
  already produced that data). The bridge's own `REPLY_TIMEOUT_S` (12 s) must
  stay under `weather_http_get()`'s fixed 15 s client timeout — it owns the
  whole Telegram round-trip and only responds once it has an answer or gives
  up; a timeout (either side) just skips the overlay for that cycle, primary
  data stands. The bridge script is untested against live Telegram from this
  environment (no login path available here) — its Telethon API calls were
  verified against the installed library's real signatures, not run end-to-end.
- Weather worker waits for the first NTP sync (up to 60 s) before its first fetch
  so forecast-day `mktime` timestamps are correct.
- **Audio pins** (S3_RLCD_4_2): I2S MCLK 16 / BCLK 9 / WS 45 / DIN 10 / DOUT 8,
  PA enable 46; ES8311 `0x18`, ES7210 `0x40` on the shared I2C bus.
- **codec_board is vendored + patched**: `lcd_init.c` removed (used IDF-6-removed
  LCD APIs), legacy `driver/i2c.h` include dropped, `_i2c_init()` patched to reuse
  the bus our `i2c_bus.c` created on `I2C_NUM_0` (search "CronoIQ patch"), and
  `board_cfg.txt` `S3_RLCD_4_2` `pa_gain` lowered 6→0. Re-apply if re-vendored.
- **Playback loudness**: `esp_codec_dev` ES8311 driver does
  `dac_db -= (20·log10(3.3/5.0) + pa_gain)`. `board_cfg.txt` `pa_gain` is the only
  real loudness lever above `CONFIG_VOICE_SPEAKER_VOL` (which maxes at 100).
  Lower `pa_gain` = louder / more clip risk. All I2S pins verified against the
  board GPIO map — playback→ES8311 (DSDIN 8), mic→ES7210 (ASDOUT 10).
- **Voice pipeline has no AEC** — WakeNet detection pauses while a session
  records/plays (`voice_session_active()` gate in `voice_wakenet.c`).
- **WakeNet is ON** (`CONFIG_VOICE_WAKENET_ENABLE=y`): esp-sr AFE in
  `voice_wakenet.c` owns the mic; `voice_wakeword.c` (DIY listener) is `#if`-gated
  off. Model `wn9_hiesp` ("Hi, ESP") packed into the `model` partition
  (`partitions.csv`), flashed with the app. esp-sr adds ~1.4 MB to the binary
  (now ~3 MB / 27 % free in the 4 MB slot).
- **esp-sr ⇒ mbedTLS OOM**: esp-sr eats internal RAM, so the TLS handshake for
  the weather fetch fails with `PSA error 0xffffff73` (-141, insufficient memory)
  unless `CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=y` + `CONFIG_MBEDTLS_DYNAMIC_BUFFER=y`
  (both in `sdkconfig.defaults`) and AFE uses `AFE_MEMORY_ALLOC_MORE_PSRAM`.
- **Wake word "Ehi Ciccio"**: `voice_enroll.c` records N clips (Kconfig
  `VOICE_ENROLL_SAMPLES`) to `/sdcard/wakeword/ehiciccio_*.wav` + an `enrolled`
  marker; `voice_wakeword_enrolled()` drives page 3's status. `voice_wakeword.c`
  is the **always-on listener** — starts once enrolled, endpoints utterances and
  calls `ww_match()`, which is a **stub returning false** (finish it with esp-dsp
  MFCC+DTW, or use the esp-sr path). `voice_wakenet.c` (esp-sr AFE) still exists
  behind `CONFIG_VOICE_WAKENET_ENABLE`, untested, esp-sr not in deps by default.
- **Voice trigger paths** all funnel through `voice.c:kick()` → session task:
  `voice_trigger()` = memo (page 4), `voice_enroll_start()` = enrollment (page 4),
  `voice_wake_response(pcm,n)` = wake response (page 3 "Here we go!" + play `pcm`,
  or enrollment clip 1 if pcm is NULL). `voice_notify_wake()` is an alias for
  `voice_wake_response(NULL,0)`. `voice_session_active()` is true for all.
  Enrollment needs the SD card (clips ~150 KB, too big for NVS) — no SD →
  `VOICE_EVT_ENROLL_FAIL`.
- **Overlay routing** (`ui.c:s_overlay`): `OVL_VOICE` → page 4 + `page_voice_update`;
  `OVL_WAKE` → page 3 + `page_message_update` (with `page_message_set_wake(true)`);
  `app_voice.c:on_evt` picks `ui_enter_wake`/`ui_exit_wake` vs the voice pair off
  `VOICE_EVT_WAKE_RESPONSE` vs `VOICE_EVT_WAKE`. Wake response auto-returns after
  playback + 1.2 s; the memo/enrollment wait for KEY.
- **KEY routing** (`on_button` in `app_main.c`): `app_voice_on_key()` first — it
  consumes *every* KEY press while a voice overlay is up. Otherwise:
  single-click → `ui_next_page()`; long-press → dispatched by `ui_current_page()`
  (0 = `time_sync_resync()`, 1 = `app_weather_request_refresh()`,
  2 = `app_voice_enroll_start()`). BOOT long-press → `voice_wake_response(NULL,0)`
  (simulate wake word); BOOT double-click → `voice_trigger()` (memo test).
- Page 4 (`SCREEN_VOICE = 3` in `ui.c`) is outside the `PAGE_COUNT` cycle;
  `ui_enter_voice()` / `ui_exit_voice()` are driven from the voice callback and
  save/restore the page you were on.

## Common tasks

**Add a weather provider**: new `components/weather/provider_x.c` exporting
`weather_provider_x(const weather_query_t*, weather_data_t*)`; add it to the
dispatch `if` chain in `weather_fetch()` and the `SRCS` list in `weather.c` /
CMake. Write the full condition text straight into `cur_cond`/`days[].cond`
(icon selection reads it back out via `weather_condition_key()` — see
`provider_openmeteo.c`'s `weather_wmo_text()` for a from-code-to-text example).
Doesn't have to be JSON — `provider_telegram_cml.c` parses plain text instead.

**Add a non-forecast current-conditions overlay** (like the Telegram one): a
provider that only ever sets `cur_*`/`location_label` and leaves `day_count` at
0 can either be selected outright via `weather.provider`, or layered on top of
another provider's forecast — see `app_weather.c:apply_telegram_overlay()` for
the pattern (a config `*_enabled` switch + a short-circuit when that provider is
already the primary one).

**Add a page**: `main/ui/page_x.c` with `page_x_create(parent)` + `page_x_update()`,
declare in `ui_internal.h`, bump `PAGE_COUNT` and the `s_screens`/`switch` blocks
in `ui.c`, add to `main/CMakeLists.txt`.

**Tune the voice memo**: Kconfig under `Voice memo` — `VOICE_SILENCE_DBFS`,
`VOICE_TRAILING_SILENCE_MS`, `VOICE_MIN_SPEECH_MS`, `VOICE_MAX_RECORD_MS`,
`VOICE_MIC_GAIN_DB`, `VOICE_SPEAKER_VOL`. Silence detection is plain RMS in
`voice.c:frame_dbfs()` — no esp-sr needed.

**Change a default**: `DEF_*` macros at the top of `main/app_config.c`.
