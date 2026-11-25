idf_component_register(SRCS "servo.c" "servo_cmd.c"
                       INCLUDE_DIRS "include"
                       REQUIRES pca9685 board console)
