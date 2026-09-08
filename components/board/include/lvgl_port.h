// LVGL v9 glue for the ST7305 reflective panel.
#pragma once

#include <stdbool.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// Init LVGL, create the 400x300 display bound to the ST7305 driver, and start
// the LVGL handler task. rlcd_init() must have run first.
void lvgl_port_init(void);

// Recursive-style lock around every LVGL API call made outside the LVGL task.
// timeout_ms < 0 waits forever. Returns true if the lock was taken.
bool lvgl_lock(int timeout_ms);
void lvgl_unlock(void);

// Flash the panel black/white to clear reflective-LCD ghosting, then force a
// full repaint of the active screen. The caller must hold the LVGL lock.
void lvgl_port_antighost(void);

#ifdef __cplusplus
}
#endif
