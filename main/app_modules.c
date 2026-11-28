#include <stdio.h>
#include <string.h>
#include "sdkconfig.h"
#include "app_modules.h"
#include "esp_log.h"
#include "esp_console.h"

#include "board.h"
#include "battery.h"
#include "pca9685.h"
#include "servo.h"
#include "motor.h"
#include "imu.h"
#include "audio.h"
#include "display.h"
#include "kcam.h"

static const char *TAG = "app";

/* true when a bool option from menuconfig is set (an unset option is simply not defined) */
#define OPTION_VALUE_1  1,
#define OPTION_PICK(_ignored, value, ...) value
#define OPTION_TEST(...) OPTION_PICK(__VA_ARGS__)
#define OPTION_EXPAND(v) OPTION_TEST(OPTION_VALUE_##v true, false)
#define IS_ON(option) OPTION_EXPAND(option)

static esp_err_t init_pca9685(void)
{
    return pca9685_init(50);    /* 50 Hz for the servos; the motor direction lines are static levels */
}

static esp_err_t init_audio(void)
{
    return audio_init(CONFIG_KERO_AUDIO_SAMPLE_RATE);
}

/* Order matters: a module comes after the one it needs. */
static app_module_t s_modules[] = {
    { .name = "i2c",     .enabled = true,                                       .init = board_i2c_init, .register_commands = board_register_commands },
    { .name = "battery", .enabled = IS_ON(CONFIG_KERO_ENABLE_BATTERY),     .init = battery_init,   .register_commands = battery_register_commands },
    { .name = "pca9685", .enabled = IS_ON(CONFIG_KERO_ENABLE_PCA9685),     .init = init_pca9685,   .register_commands = pca9685_register_commands, .needs = "i2c" },
    { .name = "servo",   .enabled = IS_ON(CONFIG_KERO_ENABLE_SERVO),       .init = servo_init,     .register_commands = servo_register_commands,   .needs = "pca9685" },
    { .name = "motor",   .enabled = IS_ON(CONFIG_KERO_ENABLE_MOTOR),       .init = motor_init,     .register_commands = motor_register_commands,   .needs = "pca9685" },
    { .name = "imu",     .enabled = IS_ON(CONFIG_KERO_ENABLE_IMU),         .init = imu_init,       .register_commands = imu_register_commands,     .needs = "i2c" },
    { .name = "audio",   .enabled = IS_ON(CONFIG_KERO_ENABLE_AUDIO),       .init = init_audio,     .register_commands = audio_register_commands },
    { .name = "display", .enabled = IS_ON(CONFIG_KERO_ENABLE_DISPLAY),     .init = display_init,   .register_commands = display_register_commands },
    { .name = "camera",  .enabled = IS_ON(CONFIG_KERO_ENABLE_CAMERA),      .init = kcam_init,       .register_commands = kcam_register_commands },
};

#define MODULE_COUNT (sizeof(s_modules) / sizeof(s_modules[0]))

static app_module_t *find_module(const char *name)
{
    for (size_t i = 0; i < MODULE_COUNT; i++) {
        if (strcmp(s_modules[i].name, name) == 0) {
            return &s_modules[i];
        }
    }
    return NULL;
}

static bool module_is_up(const app_module_t *m)
{
    return m && m->tried && m->result == ESP_OK;
}

static void init_one(app_module_t *m)
{
    if (m->needs && !module_is_up(find_module(m->needs))) {
        m->tried = true;
        m->result = ESP_ERR_INVALID_STATE;
        ESP_LOGW(TAG, "%-8s skipped: needs '%s'", m->name, m->needs);
        return;
    }
    m->result = m->init();
    m->tried = true;
    if (m->result == ESP_OK) {
        ESP_LOGI(TAG, "%-8s ok", m->name);
    } else {
        ESP_LOGE(TAG, "%-8s FAILED: %s", m->name, esp_err_to_name(m->result));
    }
}

void app_modules_init_all(void)
{
    for (size_t i = 0; i < MODULE_COUNT; i++) {
        if (s_modules[i].enabled) {
            init_one(&s_modules[i]);
        }
    }
}

void app_modules_print_status(void)
{
    printf("module    state\n");
    for (size_t i = 0; i < MODULE_COUNT; i++) {
        const app_module_t *m = &s_modules[i];
        printf("%-9s ", m->name);
        if (!m->enabled) {
            printf("disabled in menuconfig\n");
        } else if (!m->tried) {
            printf("not initialised\n");
        } else if (m->result == ESP_OK) {
            printf("ok\n");
        } else if (m->needs && !module_is_up(find_module(m->needs))) {
            printf("skipped, needs '%s'\n", m->needs);
        } else {
            printf("FAILED (%s)\n", esp_err_to_name(m->result));
        }
    }
}

static int cmd_status(int argc, char **argv)
{
    app_modules_print_status();
    return 0;
}

/* init <module|all>: run a module's init again, e.g. after plugging in the power board. */
static int cmd_init(int argc, char **argv)
{
    if (argc != 2) {
        printf("usage: init <module|all>   (modules:");
        for (size_t i = 0; i < MODULE_COUNT; i++) {
            printf(" %s", s_modules[i].name);
        }
        printf(")\n");
        return 1;
    }
    if (strcmp(argv[1], "all") == 0) {
        for (size_t i = 0; i < MODULE_COUNT; i++) {
            if (s_modules[i].enabled && !module_is_up(&s_modules[i])) {
                init_one(&s_modules[i]);
            }
        }
        app_modules_print_status();
        app_face_start();
        return 0;
    }
    app_module_t *m = find_module(argv[1]);
    if (!m) {
        printf("unknown module '%s'\n", argv[1]);
        return 1;
    }
    init_one(m);
    if (strcmp(m->name, "display") == 0) {
        app_face_start();   /* the panel was reset: draw the whole face again */
    }
    printf("%s: %s\n", m->name, m->result == ESP_OK ? "ok" : esp_err_to_name(m->result));
    return m->result == ESP_OK ? 0 : 1;
}

void app_modules_register_commands(void)
{
    /* Commands are registered even for modules that failed: they answer "not initialised". */
    for (size_t i = 0; i < MODULE_COUNT; i++) {
        if (s_modules[i].register_commands) {
            s_modules[i].register_commands();
        }
    }
    const esp_console_cmd_t status = { .command = "status", .help = "which modules came up", .func = cmd_status };
    esp_console_cmd_register(&status);
    const esp_console_cmd_t init = { .command = "init", .help = "init <module|all>: retry a module that failed", .func = cmd_init };
    esp_console_cmd_register(&init);
}
