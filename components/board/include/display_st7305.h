// ST7305/ST7306-class reflective monochrome LCD (400x300) on the
// Waveshare ESP32-S3-RLCD-4.2. 1bpp packed framebuffer, full-frame refresh.
#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RLCD_WIDTH  400
#define RLCD_HEIGHT 300

// Bring up SPI + panel and clear it to white. Call once.
void rlcd_init(void);

// Fill the whole framebuffer. white=true -> blank (reflective white), false -> black.
void rlcd_fill(bool white);

// Set a single pixel in the framebuffer. white=true -> white, false -> black (ink).
void rlcd_pixel(uint16_t x, uint16_t y, bool white);

// Push the framebuffer to the panel.
void rlcd_refresh(void);

// Anti-ghost: flash the panel fully black then fully white (leaves it white).
// The caller is expected to redraw content afterwards. Blocks ~180 ms.
void rlcd_flash(void);

#ifdef __cplusplus
}
#endif
