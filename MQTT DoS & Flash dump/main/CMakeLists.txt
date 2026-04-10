idf_component_register(SRCS "main.c"
                    INCLUDE_DIRS "."
                    REQUIRES mqtt esp_wifi nvs_flash esp_event esp_netif driver)

# idf_component_register(SRCS "main.c"
#                     INCLUDE_DIRS "."
#                     REQUIRES esp_wifi esp_event nvs_flash mqtt driver)