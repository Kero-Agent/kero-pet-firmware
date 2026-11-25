#include <math.h>
#include <stdlib.h>
#include "audio.h"
#include "board.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"

static const char *TAG = "audio";

#define CHUNK_FRAMES    128     /* frames converted per I2S call */

static i2s_chan_handle_t s_tx, s_rx;
static uint32_t s_rate;
static int s_volume = 30;
static int s_mic_shift;
static bool s_ready;

bool audio_is_ready(void)
{
    return s_ready;
}

uint32_t audio_sample_rate(void)
{
    return s_rate;
}

void audio_set_volume(int percent)
{
    s_volume = percent < 0 ? 0 : (percent > 100 ? 100 : percent);
}

int audio_get_volume(void)
{
    return s_volume;
}

void audio_set_mic_gain_shift(int shift)
{
    s_mic_shift = shift < 0 ? 0 : (shift > 8 ? 8 : shift);
}

esp_err_t audio_init(uint32_t sample_rate_hz)
{
    if (s_ready) {
        return ESP_OK;
    }
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;     /* send silence when nothing is queued: no noise from stale data */
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, &s_tx, &s_rx), TAG, "i2s_new_channel");

    const i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate_hz),
        /* 32-bit slots: the ICS-43434 needs 64 BCLK per frame and delivers 24 bits, MSB first */
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = BOARD_I2S_BCLK,
            .ws = BOARD_I2S_WS,
            .dout = BOARD_I2S_DOUT,
            .din = BOARD_I2S_DIN,
        },
    };
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_tx, &std_cfg), TAG, "tx init");
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_rx, &std_cfg), TAG, "rx init");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_tx), TAG, "tx enable");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_rx), TAG, "rx enable");

    s_rate = sample_rate_hz;
    s_ready = true;
    ESP_LOGI(TAG, "I2S ready: %u Hz, BCLK=%d WS=%d DOUT=%d DIN=%d",
             (unsigned)sample_rate_hz, BOARD_I2S_BCLK, BOARD_I2S_WS, BOARD_I2S_DOUT, BOARD_I2S_DIN);
    return ESP_OK;
}

esp_err_t audio_play(const int16_t *samples, size_t count)
{
    ESP_RETURN_ON_FALSE(s_ready, ESP_ERR_INVALID_STATE, TAG, "not initialised");
    int32_t frame[CHUNK_FRAMES * 2];
    while (count) {
        const size_t n = count > CHUNK_FRAMES ? CHUNK_FRAMES : count;
        for (size_t i = 0; i < n; i++) {
            /* 16-bit sample scaled by the volume into the top of a 32-bit slot */
            const int32_t v = (int32_t)samples[i] * s_volume / 100 * 65536;
            frame[2 * i] = v;       /* left: the amplifier plays this slot */
            frame[2 * i + 1] = v;   /* right: same data, harmless */
        }
        size_t written = 0;
        ESP_RETURN_ON_ERROR(i2s_channel_write(s_tx, frame, n * 2 * sizeof(int32_t), &written, portMAX_DELAY), TAG, "i2s write");
        samples += n;
        count -= n;
    }
    return ESP_OK;
}

esp_err_t audio_play_tone(uint32_t freq_hz, uint32_t duration_ms)
{
    ESP_RETURN_ON_FALSE(s_ready, ESP_ERR_INVALID_STATE, TAG, "not initialised");
    ESP_RETURN_ON_FALSE(freq_hz > 0 && freq_hz < s_rate / 2, ESP_ERR_INVALID_ARG, TAG, "frequency out of range");
    int16_t buf[CHUNK_FRAMES];
    const size_t total = (size_t)((uint64_t)s_rate * duration_ms / 1000);
    const size_t fade = s_rate / 100;   /* 10 ms ramps: no click at start and end */
    const float step = 2.0f * (float)M_PI * (float)freq_hz / (float)s_rate;
    float phase = 0;
    size_t done = 0;
    while (done < total) {
        const size_t n = (total - done) > CHUNK_FRAMES ? CHUNK_FRAMES : (total - done);
        for (size_t i = 0; i < n; i++) {
            const size_t pos = done + i;
            float env = 1.0f;
            if (pos < fade) {
                env = (float)pos / fade;
            } else if (total - pos < fade) {
                env = (float)(total - pos) / fade;
            }
            buf[i] = (int16_t)(sinf(phase) * 30000.0f * env);
            phase += step;
            if (phase > 2.0f * (float)M_PI) {
                phase -= 2.0f * (float)M_PI;
            }
        }
        ESP_RETURN_ON_ERROR(audio_play(buf, n), TAG, "play");
        done += n;
    }
    return ESP_OK;
}

esp_err_t audio_record(int16_t *samples, size_t count)
{
    ESP_RETURN_ON_FALSE(s_ready, ESP_ERR_INVALID_STATE, TAG, "not initialised");
    int32_t frame[CHUNK_FRAMES * 2];
    while (count) {
        const size_t n = count > CHUNK_FRAMES ? CHUNK_FRAMES : count;
        size_t got = 0;
        ESP_RETURN_ON_ERROR(i2s_channel_read(s_rx, frame, n * 2 * sizeof(int32_t), &got, portMAX_DELAY), TAG, "i2s read");
        const size_t frames = got / (2 * sizeof(int32_t));
        for (size_t i = 0; i < frames; i++) {
            /* left slot, 24 valid bits at the top; keep the upper 16 and apply the digital gain */
            int32_t v = (frame[2 * i] >> (16 - s_mic_shift));
            if (v > INT16_MAX) {
                v = INT16_MAX;
            }
            if (v < INT16_MIN) {
                v = INT16_MIN;
            }
            samples[i] = (int16_t)v;
        }
        samples += frames;
        count -= frames;
    }
    return ESP_OK;
}

esp_err_t audio_measure_level(uint32_t duration_ms, audio_level_t *out)
{
    ESP_RETURN_ON_FALSE(s_ready, ESP_ERR_INVALID_STATE, TAG, "not initialised");
    ESP_RETURN_ON_FALSE(out, ESP_ERR_INVALID_ARG, TAG, "null");
    int16_t buf[CHUNK_FRAMES];
    /* drop the first 100 ms: the microphone needs a moment after the clock starts */
    size_t skip = s_rate / 10;
    while (skip) {
        const size_t n = skip > CHUNK_FRAMES ? CHUNK_FRAMES : skip;
        ESP_RETURN_ON_ERROR(audio_record(buf, n), TAG, "record");
        skip -= n;
    }
    size_t total = (size_t)((uint64_t)s_rate * duration_ms / 1000);
    if (total == 0) {
        total = CHUNK_FRAMES;
    }
    double sum = 0, sum_sq = 0;
    int peak = 0;
    size_t done = 0;
    while (done < total) {
        const size_t n = (total - done) > CHUNK_FRAMES ? CHUNK_FRAMES : (total - done);
        ESP_RETURN_ON_ERROR(audio_record(buf, n), TAG, "record");
        for (size_t i = 0; i < n; i++) {
            sum += buf[i];
            sum_sq += (double)buf[i] * buf[i];
            const int a = abs(buf[i]);
            if (a > peak) {
                peak = a;
            }
        }
        done += n;
    }
    const double mean = sum / total;
    double var = sum_sq / total - mean * mean;
    if (var < 1e-9) {
        var = 1e-9;
    }
    /* RMS of a full-scale sine is 32768 / sqrt(2) */
    out->rms_dbfs = (float)(10.0 * log10(var / (32768.0 * 32768.0 / 2.0)));
    out->peak_dbfs = (float)(20.0 * log10((peak > 0 ? peak : 1) / 32768.0));
    out->dc_offset = (int32_t)mean;
    return ESP_OK;
}
