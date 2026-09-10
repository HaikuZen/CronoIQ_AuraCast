// Shared between voice.c / voice_enroll.c / voice_wakeword.c.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "voice.h"
#include "sdkconfig.h"

#define VOICE_RATE          CONFIG_VOICE_SAMPLE_RATE
#define VOICE_FRAME_MS      20
#define VOICE_FRAME_SAMPLES (VOICE_RATE * VOICE_FRAME_MS / 1000)
#define VOICE_MAX_SAMPLES   ((size_t)VOICE_RATE * CONFIG_VOICE_MAX_RECORD_MS / 1000)

#define VOICE_WW_DIR    "/sdcard/wakeword"
#define VOICE_WW_MARKER VOICE_WW_DIR "/enrolled"

// Emit an event to the registered callback (voice.c).
void voice_emit(voice_evt_t e, int elapsed_ms, int total_ms, int level,
                int enroll_index, int enroll_total);

// True (and clears) if a KEY press is pending (voice.c).
bool voice_key_taken(void);

// RMS of a frame -> dBFS (approx, -96..0). (voice.c)
float voice_frame_dbfs(const int16_t *p, size_t n);
int   voice_level_from_dbfs(float dbfs);

// Shared PSRAM capture buffer (voice.c owns it).
extern int16_t *voice_buf;
extern size_t   voice_total;

// Wake detected -> start a memo session (voice.c).
void voice_notify_wake(void);

// Enrollment (voice_enroll.c).
void voice_enroll_run(void);
bool voice_enroll_scan(int *count_out);            // scan SD, return enrolled?
size_t voice_enroll_load_clip(int index, int16_t *dst, size_t cap);  // WAV -> PCM

// Always-on listener (voice_wakeword.c).
void voice_wakeword_listener_start(void);
