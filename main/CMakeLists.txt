idf_component_register(SRCS "app_main.c" "app_modules.c" "app_preview.c" "app_face.c" "app_image.c" "face_render.c"
                       INCLUDE_DIRS "."
                       REQUIRES board battery pca9685 servo motor imu audio display cam shell console esp_timer esp_driver_usb_serial_jtag)
