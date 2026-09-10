#pragma once

#include <stdbool.h>
#include "voice.h"
#include "button.h"

#ifdef __cplusplus
extern "C" {
#endif

void app_voice_start(void);

// Start "Ehi Ciccio" wake-word enrollment (page 3 KEY long-press).
void app_voice_enroll_start(void);

// Wake-word enrollment state, for page 3.
bool app_voice_ww_enrolled(void);
int  app_voice_ww_count(void);

// Latest voice status snapshot. Returns false before the first event.
bool app_voice_status(voice_status_t *out);

// Route a button press. Returns true if the voice feature consumed it
// (long-press trigger, or any KEY press while a session is active).
bool app_voice_on_key(board_btn_t btn, board_btn_evt_t evt);

#ifdef __cplusplus
}
#endif
