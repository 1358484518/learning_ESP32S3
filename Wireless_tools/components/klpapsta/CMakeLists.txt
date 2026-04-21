idf_component_register(SRCS "klpapsta.c"
                    INCLUDE_DIRS "include"
                    REQUIRES esp_wifi nvs_flash klphttpserver)
