idf_component_register(SRCS "app_main.c" "app_modules.c" "app_preview.c"
                       INCLUDE_DIRS "."
                       REQUIRES board battery pca9685 servo motor imu audio display cam shell console esp_timer)
