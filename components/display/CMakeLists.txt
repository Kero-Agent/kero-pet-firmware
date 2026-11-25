# Panel driver: swap display_panel_st77916.c for another display_panel_xxx.c to use a different screen.
idf_component_register(SRCS "display.c" "display_cmd.c" "display_panel_st77916.c"
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS "."
                       REQUIRES board esp_lcd esp_driver_spi esp_driver_ledc console)
