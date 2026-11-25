idf_component_register(SRCS "motor.c" "motor_cmd.c"
                       INCLUDE_DIRS "include"
                       REQUIRES pca9685 board esp_driver_ledc esp_timer console)
