#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "shell.h"
#include "sdkconfig.h"
#include "esp_console.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_system.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_idf_version.h"

#if !CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
#error "Set the console to USB Serial/JTAG (CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG). UART0 pins GPIO43/44 are the LCD backlight and the microphone data line on this board."
#endif

static const char *TAG = "shell";

static esp_console_repl_t *s_repl;

static const char *reset_reason_name(esp_reset_reason_t r)
{
    switch (r) {
    case ESP_RST_POWERON:   return "power on";
    case ESP_RST_SW:        return "software reset";
    case ESP_RST_PANIC:     return "panic";
    case ESP_RST_INT_WDT:   return "interrupt watchdog";
    case ESP_RST_TASK_WDT:  return "task watchdog";
    case ESP_RST_WDT:       return "other watchdog";
    case ESP_RST_BROWNOUT:  return "brownout";
    case ESP_RST_USB:       return "USB";
    case ESP_RST_JTAG:      return "JTAG";
    default:                return "other";
    }
}

static int cmd_sys(int argc, char **argv)
{
    esp_chip_info_t chip;
    esp_chip_info(&chip);
    uint32_t flash_size = 0;
    esp_flash_get_size(NULL, &flash_size);
    const int64_t up_ms = esp_timer_get_time() / 1000;
    printf("ESP-IDF %s, chip rev v%d.%d, %d core(s), flash %" PRIu32 " MB\n",
           esp_get_idf_version(), chip.revision / 100, chip.revision % 100, chip.cores, flash_size / (1024 * 1024));
    printf("uptime %lld.%03lld s, last reset: %s\n", up_ms / 1000, up_ms % 1000, reset_reason_name(esp_reset_reason()));
    printf("heap internal: %u free, %u minimum ever\n",
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL), (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL));
    printf("heap PSRAM:    %u free of %u\n",
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM), (unsigned)heap_caps_get_total_size(MALLOC_CAP_SPIRAM));
    return 0;
}

static int cmd_reboot(int argc, char **argv)
{
    printf("restarting...\n");
    fflush(stdout);
    esp_restart();
    return 0;
}

static int cmd_log(int argc, char **argv)
{
    static const char *names[] = { "none", "error", "warn", "info", "debug", "verbose" };
    if (argc == 3) {
        for (int lv = 0; lv < 6; lv++) {
            if (strcmp(argv[2], names[lv]) == 0) {
                esp_log_level_set(argv[1], (esp_log_level_t)lv);
                printf("log level of '%s' set to %s\n", argv[1], names[lv]);
                return 0;
            }
        }
    }
    printf("usage: log <tag|*> <none|error|warn|info|debug|verbose>\n"
           "tags: board pca9685 servo motor battery imu audio display cam face app\n");
    return 1;
}

esp_err_t shell_init(void)
{
    if (s_repl) {
        return ESP_OK;
    }
    esp_console_repl_config_t repl_cfg = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_cfg.prompt = "kero>";
    repl_cfg.max_cmdline_length = 256;
    repl_cfg.task_stack_size = 12 * 1024;   /* commands run in this task: audio and camera tests need room */
    esp_console_dev_usb_serial_jtag_config_t hw_cfg = ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_console_new_repl_usb_serial_jtag(&hw_cfg, &repl_cfg, &s_repl), TAG, "console create failed");

    esp_console_register_help_command();
    const esp_console_cmd_t cmds[] = {
        { .command = "sys",    .help = "chip, uptime, reset reason and heap", .func = cmd_sys },
        { .command = "reboot", .help = "restart the ESP32",                   .func = cmd_reboot },
        { .command = "log",    .help = "log <tag|*> <none|error|warn|info|debug|verbose>", .func = cmd_log },
    };
    for (size_t i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++) {
        ESP_RETURN_ON_ERROR(esp_console_cmd_register(&cmds[i]), TAG, "register %s", cmds[i].command);
    }
    return ESP_OK;
}

esp_err_t shell_start(void)
{
    ESP_RETURN_ON_FALSE(s_repl, ESP_ERR_INVALID_STATE, TAG, "shell_init() first");
    return esp_console_start_repl(s_repl);
}
