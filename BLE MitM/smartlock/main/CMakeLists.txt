idf_component_register(SRCS "smartlock.c"
                    INCLUDE_DIRS "."
                    PRIV_REQUIRES nvs_flash esp_driver_gpio bt)