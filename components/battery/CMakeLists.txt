idf_component_register(SRCS "battery.c" "battery_cmd.c"
                       INCLUDE_DIRS "include"
                       REQUIRES board esp_adc console)
