#include "http_demo.h"
#include "esp_http_client.h"
#include "esp_log.h"

static const char *TAG = "http_demo";

void http_demo(void)
{
    esp_http_client_config_t config = {
        .url = "http://192.168.1.137:8000/login",
        .method = HTTP_METHOD_POST,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);

    const char *post_data = "username=admin&password=1234";

    esp_http_client_set_header(client, "Content-Type", "application/x-www-form-urlencoded");
    esp_http_client_set_post_field(client, post_data, strlen(post_data));

    esp_http_client_perform(client);
    esp_http_client_cleanup(client);
}