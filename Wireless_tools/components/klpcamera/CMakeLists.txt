idf_component_register(SRCS "klpcamera.c"
                    INCLUDE_DIRS "include"
                    REQUIRES espressif__esp32-camera nvs_flash esp_timer)
