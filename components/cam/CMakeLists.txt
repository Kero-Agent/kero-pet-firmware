idf_component_register(SRCS "cam.c" "cam_cmd.c"
                       INCLUDE_DIRS "include"
                       REQUIRES board esp_timer console)
