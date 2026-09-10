// Thin wrapper over codec_board / esp_codec_dev for the voice feature.
// Records/plays mono 16-bit; the ES7210/ES8311 run 2-channel and this file
// down/up-mixes at the edges.
#pragma once

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

esp_err_t voice_codec_init(int sample_rate);

// Idempotent: opens the mic stream once and keeps it open.
esp_err_t voice_codec_rec_open(int mic_gain_db);
esp_err_t voice_codec_rec_read(int16_t *mono, size_t samples, size_t *got);
// Raw interleaved stereo (2 ch) frames, for the WakeNet AFE feed.
esp_err_t voice_codec_rec_read_raw(int16_t *interleaved, size_t frames);
int       voice_codec_channels(void);
void      voice_codec_rec_close(void);

esp_err_t voice_codec_play_open(int speaker_vol);
esp_err_t voice_codec_play_write(const int16_t *mono, size_t samples);
void      voice_codec_play_close(void);
