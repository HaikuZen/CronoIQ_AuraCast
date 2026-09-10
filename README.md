# CronoIQ_AuraCast

Firmware for the **Waveshare ESP32-S3-RLCD-4.2** — a battery-capable desk clock
built around a 4.2", 400×300 monochrome *reflective* LCD (no backlight, e‑paper‑like
look, ST7305/ST7306-class controller).

Three full-screen pages, cycled with the **KEY** button:

| # | Page | Contents |
|---|------|----------|
| 1 | **Clock** | Weekday, date, large 7‑segment `HH:MM`. Indoor temperature + humidity from the on-board SHTC3. An outdoor weather badge (icon + temp). Status line: Wi‑Fi, time source (NTP vs RTC), battery %. |
| 2 | **Weather** | City name, a weather icon + the full condition text (e.g. *"Rain, Partially cloudy"*), current / feels-like temperature, then a 1–5 day forecast — each day with its own icon, min/max, condition text and precipitation probability. |
| 3 | **Hello!** | Placeholder text + wake-word status. On a wake word it flips to **"Here we go!"** and plays back the captured audio. |
| 4 | **Voice memo** | Record the mic (≤ 30 s, stops on silence), play it back, level meter / waveform. Also the wake-word enrollment screen. Stays up until KEY. |

* **KEY button** (GPIO18)
  * single click → next page (wraps 3 → 1)
  * long press, **page-specific**:
    | Page | KEY long-press |
    |---|---|
    | 1 Clock | force an NTP re-sync now |
    | 2 Weather | force a weather refresh now |
    | 3 Hello! | record / re-record the **"Ehi Ciccio"** wake word |
  * while page 4 / a wake response is up → stops the recording / playback / step
* **BOOT button** (GPIO0)
  * long press → **simulate the wake word** (page 3 "Here we go!" + play the
    enrolled clip) — the way to test the flow while `ww_match()` is a stub
  * double click → voice memo (record + playback), for testing the audio path

### Wake word

Hands-free detection uses **esp-sr WakeNet** (`CONFIG_VOICE_WAKENET_ENABLE=y`).
The intended phrase is **"Ehi Ciccio"**, but that is not a stock WakeNet model —
the build ships **"Hi, ESP"** (`CONFIG_SR_WN_WN9_HIESP`). Pick another with
`idf.py menuconfig → ESP Speech Recognition → Select wake words`, or obtain a
custom "Ehi Ciccio" model from Espressif's
[customization service](https://docs.espressif.com/projects/esp-sr/en/latest/esp32s3/wake_word_engine/ESP_Wake_Words_Customization.html)
and register it there.

The model is packed into the `model` partition (`partitions.csv`) and flashed by
`idf.py flash` alongside the app — the layout changed, so **`idf.py erase-flash`
once** after enabling.

When the wake word fires, the device jumps to **page 3**, shows **"Here we go!"**,
plays back the first enrolled clip, and reverts. `components/voice/voice_enroll.c`
still records "Ehi Ciccio" samples to `/sdcard/wakeword/` (page 3 long-press) —
that's what gets played back on a wake, and page 3 shows the enrolled state.

Test gestures while WakeNet warms up: **BOOT long-press** simulates a wake,
**BOOT double-click** runs a voice memo.

To go back to the lean build (no esp-sr): set `CONFIG_VOICE_WAKENET_ENABLE=n`,
remove the `model` partition line, and drop `espressif/esp-sr` from
`components/voice/idf_component.yml`. `components/voice/voice_wakeword.c` is the
DIY fallback listener (currently a stub `ww_match()`).

The clock is seeded from the PCF85063 RTC at boot, disciplined by NTP once online,
and the NTP time is written back to the RTC so it survives a power cut. Weather is
refreshed every 30 minutes; the last good result is kept (and marked *stale*) if a
fetch fails.

---

## Building

Requires **ESP-IDF ≥ 5.3** (developed and CI-built against **v6.1**). LVGL v9 and
cJSON are pulled automatically by the component manager on first configure.

```sh
. $IDF_PATH/export.sh
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

The board enumerates as a USB CDC/JTAG device (`/dev/ttyACM*` on Linux); console
logs come out over that same USB port at 115200 baud.

---

## Configuration

At boot the firmware mounts the microSD card (FAT32) and reads
**`/sdcard/config.json`**. Every field is optional — anything missing, and the
whole file or card being absent, falls back to the compiled-in defaults in
[`main/app_config.c`](main/app_config.c). Edit those defaults for a card-less build.

Copy [`sdcard/config.example.json`](sdcard/config.example.json) to the card root and
edit:

```json
{
  "wifi":    { "ssid": "YourNetwork", "password": "YourPassword" },
  "weather": {
    "provider": "visualcrossing",
    "api_key": "YOUR_KEY",
    "location": "45.4642,9.1900",
    "forecast_days": 3,
    "metric": true
  },
  "time":    { "tz": "CET-1CEST,M3.5.0,M10.5.0/3", "ntp_server": "pool.ntp.org" },
  "sensor":  { "temp_offset_dc": -40 }
}
```

| Key | Meaning |
|---|---|
| `wifi.ssid` / `wifi.password` | Station credentials. Open networks: leave `password` empty. |
| `weather.provider` | `visualcrossing` \| `openmeteo` \| `openweathermap`. An unknown value falls back to Visual Crossing. |
| `weather.api_key` | Required for Visual Crossing and OpenWeatherMap. Ignored by Open-Meteo. |
| `weather.location` | `"lat,lon"` works for **all** providers. Visual Crossing also accepts a place name (e.g. `"Torino,IT"`). |
| `weather.city` | Optional label shown as the page title. Leave `""` to auto-resolve the place name from the coordinates (keyless reverse geocoding); set it to override. |
| `weather.forecast_days` | 1–5. |
| `weather.metric` | `true` → °C, `false` → °F. Applies to both the forecast and the indoor sensor readout. |
| `time.tz` | POSIX TZ string. See the [zone table](https://github.com/nayarsystems/posix_tz_db/blob/master/zones.csv). |
| `time.ntp_server` | Defaults to `pool.ntp.org`. |
| `sensor.temp_offset_dc` | SHTC3 correction in **tenths of °C**. `-40` = −4.0 °C, compensating the board's self-heating (matches Waveshare's own demo). |

### Provider notes

* **Visual Crossing** — [Timeline API](https://www.visualcrossing.com/weather-api).
  Free tier = 1000 records/day; one refresh here costs ~4 records, so 30-minute
  polling is well within budget.
* **Open-Meteo** — no key, no account. `location` **must** be `"lat,lon"`. Good
  zero-config default if you don't want to sign up anywhere.
* **OpenWeatherMap** — [One Call API 3.0](https://openweathermap.org/api/one-call-3),
  needs a key with One Call enabled. `location` must be `"lat,lon"`.

---

## Hardware map

Verified against the Waveshare schematic, the official
[`waveshareteam/ESP32-S3-RLCD-4.2`](https://github.com/waveshareteam/ESP32-S3-RLCD-4.2)
demo, and the Zephyr board port.

| Function | GPIO |
|---|---|
| LCD SPI (SPI2) | SCK 11, MOSI 12, CS 40, DC 5, RST 41 (TE 6 unused) |
| I²C bus 0 | SDA 13, SCL 14 |
| SHTC3 (temp/hum) | I²C `0x70` |
| PCF85063A (RTC) | I²C `0x51`, INT GPIO15 |
| microSD (SDMMC, 1-bit) | CLK 38, CMD 21, D0 39 |
| KEY / BOOT buttons | GPIO18 / GPIO0, active-low, internal pull-up |
| Battery sense | GPIO4 = ADC1_CH3, external divider ×3 |
| Audio I2S | MCLK 16, BCLK 9, WS 45, DIN 10, DOUT 8; speaker PA enable 46 |
| Audio codecs | ES8311 (playback) I2C `0x18`, ES7210 (mic) I2C `0x40` |

Panel: 400×300, 1 bit/pixel, full-frame refresh over SPI at 10 MHz.

---

## Project layout

```
components/board/       Board support package
  display_st7305.c        SPI + ST7305 init + 1bpp landscape framebuffer (+ rlcd_flash anti-ghost)
  lvgl_port.c             LVGL v9 display + tick + handler task, threshold-to-B/W flush
  i2c_bus.c               shared i2c_master bus (port 0)
  shtc3.c  pcf85063.c     sensor + RTC drivers
  button.c + multi_button/  KEY/BOOT single/double/long-press
  sdcard.c  battery.c
components/weather/      Provider-agnostic forecast
  weather.c                dispatch, shared HTTPS GET, condition keys, reverse geocode
  provider_*.c             one file per back-end
components/codec_board/  vendored espressif/codec_board (trimmed) — ES8311/ES7210 bring-up
components/voice/        mic capture, RMS silence detection, playback, WakeNet seam
  voice.c                  session state machine (wake → record → playback → wait KEY)
  voice_codec.c            esp_codec_dev wrapper (mono 16 kHz over the 2-mic codec)
  voice_wakenet.c          esp-sr AFE integration (compiled only if CONFIG_VOICE_WAKENET_ENABLE)
main/
  app_main.c              bring-up sequence
  app_config.c            JSON loader + compiled defaults
  time_sync.c             TZ, RTC<->system clock, SNTP
  wifi_sta.c              station mode with backoff reconnect
  app_sensors.c           20 s SHTC3 poll task
  app_weather.c           30 min weather worker + on-demand refresh
  app_voice.c             voice event -> UI glue + KEY routing
  ui/                     LVGL screens, page manager, per-page update functions
  ui/seven_seg.c          font-free 7-segment renderer (clock + big temperatures)
  ui/weather_icon.c       font-free weather glyphs
  ui/mic_icon.c           font-free microphone / speaker glyph
```

### Threading

| Task | Core | Role |
|---|---|---|
| `lvgl` | 0 | `lv_timer_handler` loop; owns rendering |
| `ui` | 0 | receives KEY events, switches screen under the LVGL lock |
| `voice` | 1 | wake → record → playback state machine |
| `wn_feed` / `wn_fetch` | 1 | WakeNet AFE (only when enabled) |
| `weather` | any | Wi-Fi-gated fetch loop |
| `sensors` | any | SHTC3 polling |
| `wifi_recon` | any | reconnect backoff |
| esp_timer callbacks | — | LVGL tick source, 5 ms button scan |

Page update functions run as `lv_timer`s inside the `lvgl` task (1 s general +
120 ms for the voice meter), so they read shared state (protected by short
critical sections / mutexes) and touch LVGL objects without extra locking.

---

## Troubleshooting

| Symptom | Look at |
|---|---|
| Display mirrored / rotated / garbled | `0x36` (MADCTL) argument and the landscape pixel math in `components/board/display_st7305.c` — both were ported from the vendor demo and not yet checked on real glass. |
| `sdcard: mount failed` | Card must be FAT32; the firmware still boots on compiled defaults. |
| Clock shows a wrong/old time then jumps | Expected: RTC seed first, NTP correction within a few seconds of getting an IP. |
| Weather page stuck on "loading..." | Check `weather.*` config; monitor log shows the exact URL status and any provider parse error. |
| `wx.openmeteo: location must be "lat,lon"` | Open-Meteo / OpenWeatherMap don't geocode place names — use coordinates. |
| `esp-x509-crt-bundle ... signature verification failed` / `PSA ... error 0xffffff73` | `-141` = out of internal RAM during the TLS handshake (esp-sr eats it). Fixed by `CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=y` + `CONFIG_MBEDTLS_DYNAMIC_BUFFER=y` + AFE `AFE_MEMORY_ALLOC_MORE_PSRAM`. If it recurs, log `heap_caps_get_free_size(MALLOC_CAP_INTERNAL)` before the fetch. |
