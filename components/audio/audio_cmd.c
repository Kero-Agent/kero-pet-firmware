#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "audio.h"
#include "esp_console.h"
#include "esp_heap_caps.h"

static int report(esp_err_t err)
{
    if (err != ESP_OK) {
        printf("failed: %s\n", esp_err_to_name(err));
        return 1;
    }
    printf("ok\n");
    return 0;
}

static int cmd_spk(int argc, char **argv)
{
    if (!audio_is_ready()) {
        printf("audio module is not initialised\n");
        return 1;
    }
    if (argc >= 3 && strcmp(argv[1], "tone") == 0) {
        const uint32_t freq = strtoul(argv[2], NULL, 0);
        const uint32_t ms = argc >= 4 ? strtoul(argv[3], NULL, 0) : 500;
        return report(audio_play_tone(freq, ms));
    }
    if (argc == 3 && strcmp(argv[1], "vol") == 0) {
        audio_set_volume(atoi(argv[2]));
        printf("volume %d %%\n", audio_get_volume());
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "scale") == 0) {
        static const uint16_t notes[] = { 523, 587, 659, 698, 784, 880, 988, 1047 };
        esp_err_t err = ESP_OK;
        for (size_t i = 0; i < sizeof(notes) / sizeof(notes[0]) && err == ESP_OK; i++) {
            err = audio_play_tone(notes[i], 200);
        }
        return report(err);
    }
    printf("volume %d %%, %u Hz\n", audio_get_volume(), (unsigned)audio_sample_rate());
    printf("usage: spk tone <hz> [ms] | spk scale | spk vol <0..100>\n");
    return argc == 1 ? 0 : 1;
}

static int cmd_mic(int argc, char **argv)
{
    if (!audio_is_ready()) {
        printf("audio module is not initialised\n");
        return 1;
    }
    if (argc >= 2 && strcmp(argv[1], "level") == 0) {
        /* mic level [count] [window_ms] */
        const int count = argc >= 3 ? atoi(argv[2]) : 5;
        const uint32_t window = argc >= 4 ? strtoul(argv[3], NULL, 0) : 200;
        for (int i = 0; i < count; i++) {
            audio_level_t lv;
            const esp_err_t err = audio_measure_level(window, &lv);
            if (err != ESP_OK) {
                return report(err);
            }
            int bars = (int)((lv.rms_dbfs + 90.0f) / 2.0f);
            bars = bars < 0 ? 0 : (bars > 45 ? 45 : bars);
            printf("rms %6.1f dBFS  peak %6.1f dBFS  dc %5d  ", lv.rms_dbfs, lv.peak_dbfs, (int)lv.dc_offset);
            for (int b = 0; b < bars; b++) {
                putchar('#');
            }
            putchar('\n');
        }
        return 0;
    }
    if (argc >= 2 && strcmp(argv[1], "echo") == 0) {
        /* record <ms> and play it back: checks microphone and speaker together */
        uint32_t ms = argc >= 3 ? strtoul(argv[2], NULL, 0) : 2000;
        if (ms > 10000) {
            ms = 10000;
        }
        const size_t n = (size_t)((uint64_t)audio_sample_rate() * ms / 1000);
        int16_t *buf = heap_caps_malloc(n * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!buf) {
            buf = malloc(n * sizeof(int16_t));
        }
        if (!buf) {
            printf("out of memory\n");
            return 1;
        }
        printf("recording %u ms...\n", (unsigned)ms);
        esp_err_t err = audio_record(buf, n);
        if (err == ESP_OK) {
            printf("playing back...\n");
            err = audio_play(buf, n);
        }
        free(buf);
        return report(err);
    }
    if (argc == 3 && strcmp(argv[1], "gain") == 0) {
        audio_set_mic_gain_shift(atoi(argv[2]));
        printf("ok\n");
        return 0;
    }
    printf("usage: mic level [count] [window_ms] | mic echo [ms] | mic gain <0..8>\n");
    return 1;
}

void audio_register_commands(void)
{
    const esp_console_cmd_t spk = {
        .command = "spk",
        .help = "speaker: tone <hz> [ms] | scale | vol <0..100>",
        .func = cmd_spk,
    };
    esp_console_cmd_register(&spk);
    const esp_console_cmd_t mic = {
        .command = "mic",
        .help = "microphone: level [count] [window_ms] | echo [ms] | gain <0..8>",
        .func = cmd_mic,
    };
    esp_console_cmd_register(&mic);
}
