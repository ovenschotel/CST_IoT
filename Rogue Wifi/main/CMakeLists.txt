idf_component_register(SRCS "main.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp_timer nvs_flash esp_wifi esp_http_server json
                    EMBED_TXTFILES "html/login.html" "html/admin_login.html" "html/admin_dashboard.html")
