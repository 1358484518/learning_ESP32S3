idf_component_register(SRCS "klpgpio.c"
                    INCLUDE_DIRS "include"
                    REQUIRES mbedtls esp_driver_gpio esp_timer espressif__esp32-camera espressif__esp_jpeg
                    )
