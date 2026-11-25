/**
 * Audio: MAX98357A amplifier and ICS-43434 microphone on one I2S port.
 *
 * Both share BCLK (GPIO2) and WS (GPIO40), so playback and capture always run
 * at the same sample rate. The bus carries 32-bit stereo frames; the amplifier
 * plays the left slot and the microphone answers in the left slot.
 * The API works with 16-bit mono samples.
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Start the I2S port in full duplex. sample_rate_hz: 16000 is a good default for speech. */
esp_err_t audio_init(uint32_t sample_rate_hz);

bool audio_is_ready(void);

uint32_t audio_sample_rate(void);

/** Software volume for playback, 0..100 (default 30). The amplifier itself has fixed gain. */
void audio_set_volume(int percent);
int audio_get_volume(void);

/** Play 16-bit mono samples. Blocks until they are queued. */
esp_err_t audio_play(const int16_t *samples, size_t count);

/** Play a sine tone. Blocks for duration_ms. */
esp_err_t audio_play_tone(uint32_t freq_hz, uint32_t duration_ms);

/** Record 16-bit mono samples from the microphone. Blocks until count samples arrived. */
esp_err_t audio_record(int16_t *samples, size_t count);

/** Digital gain applied to recorded samples as a left shift, 0..8 (default 0). */
void audio_set_mic_gain_shift(int shift);

typedef struct {
    float rms_dbfs;     /* 0 dBFS = full scale sine */
    float peak_dbfs;
    int32_t dc_offset;  /* in 16-bit counts */
} audio_level_t;

/** Listen for duration_ms and report the level - quick check that the microphone works. */
esp_err_t audio_measure_level(uint32_t duration_ms, audio_level_t *out);

/** Console commands: spk, mic. */
void audio_register_commands(void);

#ifdef __cplusplus
}
#endif
