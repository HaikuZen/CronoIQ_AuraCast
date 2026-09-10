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

// Index of the page currently in the KEY cycle (0..2), or -1 while page 4 (voice)
// is showing.
int ui_current_page(void);

// Voice memo (page 4): show it (saving the current page) / restore. Called from
// the voice event callback; both take the LVGL lock internally.
void ui_enter_voice(void);
void ui_exit_voice(void);

// Wake-word response: jump to page 3 with "Here we go!" while the captured
// utterance plays back, then revert the text (staying on page 3).
void ui_enter_wake(void);
void ui_exit_wake(void);

#ifdef __cplusplus
}
#endif
