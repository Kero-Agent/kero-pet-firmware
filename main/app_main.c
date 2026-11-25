/**
 * Kero desktop robot - main board firmware.
 *
 * Bring-up build: every hardware module starts on its own and gets console
 * commands, so each part of the two boards can be tested separately over USB.
 * Type "help" at the kero> prompt.
 */
#include "app_modules.h"
#include "shell.h"
#include "esp_log.h"

static const char *TAG = "app";

void app_main(void)
{
    ESP_LOGI(TAG, "Kero firmware starting");

    app_modules_init_all();

    if (shell_init() == ESP_OK) {
        app_modules_register_commands();
        app_preview_register_commands();
        app_modules_print_status();
        shell_start();
    } else {
        ESP_LOGE(TAG, "console did not start");
    }
}
