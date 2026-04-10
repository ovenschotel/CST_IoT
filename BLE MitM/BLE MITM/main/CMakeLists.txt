idf_component_register(
    SRCS "MITM.c"
    INCLUDE_DIRS "."
    REQUIRES bt driver nvs_flash esp_wifi esp_netif esp_event esp_http_server
)