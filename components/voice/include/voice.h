// Voice memo (wake -> page 4 -> record <=30s -> playback) plus "Ehi Ciccio"
// wake-word enrollment. Real hands-free detection is a stub (voice_wakeword.c);
// the esp-sr path lives behind CONFIG_VOICE_WAKENET_ENABLE.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    VOICE_EVT_WAKE = 0,       // manual memo trigger fired -> page 4
    VOICE_EVT_WAKE_RESPONSE,  // wake word fired -> page 3, plays the captured audio
    VOICE_EVT_REC_START,
    VOICE_EVT_REC_TICK,   // periodic during recording
    VOICE_EVT_REC_DONE,
    VOICE_EVT_PLAY_START,
    VOICE_EVT_PLAY_TICK,  // periodic during playback
    VOICE_EVT_PLAY_DONE,  // playback finished; page stays until KEY
    VOICE_EVT_IDLE,       // session ended, back to listening

    VOICE_EVT_ENROLL_START,
    VOICE_EVT_ENROLL_PROMPT,   // get ready to speak sample enroll_index
    VOICE_EVT_ENROLL_REC,      // recording sample enroll_index
    VOICE_EVT_ENROLL_SAVED,    // sample enroll_index stored
    VOICE_EVT_ENROLL_DONE,     // all samples stored; page stays until KEY
    VOICE_EVT_ENROLL_FAIL,     // e.g. no SD card
} voice_evt_t;

typedef struct {
    voice_evt_t evt;
    int elapsed_ms;       // into the current phase
    int total_ms;         // recorded length (valid from REC_DONE onwards)
    int level;            // 0..100 smoothed input level, for the meter
    int enroll_index;     // 1-based, during enrollment
    int enroll_total;
} voice_status_t;

typedef void (*voice_cb_t)(const voice_status_t *st, void *ctx);

// Init codec + spawn the session task. Call once, after i2c_bus_init().
void voice_start(voice_cb_t cb, void *ctx);

// Manual voice-memo trigger (ignored while a session is running).
void voice_trigger(void);

// Wake-word fired: show page 3 "Here we go!" and play `pcm` (the captured
// utterance). If pcm is NULL / n is 0, the first enrollment clip is played
// instead. Ignored while a session is running.
void voice_wake_response(const int16_t *pcm, size_t n);

// Start wake-word enrollment (ignored while a session is running).
void voice_enroll_start(void);

// KEY pressed: stop recording early / stop playback / leave the DONE screen.
void voice_key(void);

// True between *_START and VOICE_EVT_IDLE (memo or enrollment).
bool voice_session_active(void);

// Wake-word enrollment state (checked on SD at boot, updated after enrollment).
bool voice_wakeword_enrolled(void);
int  voice_wakeword_count(void);

// Last recording (mono 16-bit at the configured sample rate), for the waveform.
const int16_t *voice_pcm(size_t *samples);
int voice_sample_rate(void);

#ifdef __cplusplus
}
#endif
