idf_component_register(SRCS "smart_weather.c"
                    INCLUDE_DIRS "."
                    PRIV_REQUIRES esp_wifi nvs_flash esp_http_client)