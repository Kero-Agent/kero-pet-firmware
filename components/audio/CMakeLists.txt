idf_component_register(SRCS "audio.c" "audio_cmd.c"
                       INCLUDE_DIRS "include"
                       REQUIRES board esp_driver_i2s console)
