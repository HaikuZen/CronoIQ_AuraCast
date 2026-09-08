#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Build the three screens and start the refresh timers. Call after
// lvgl_port_init().
void ui_start(void);

// Advance to the next page (wraps). Safe to call from any task/ISR-free context;
// the screen switch happens on the UI task.
void ui_next_page(void);

#ifdef __cplusplus
}
#endif
