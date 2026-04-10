#include "esp_http_client.h"
#include "esp_log.h"
#include <string.h>
#include <stdint.h>
#include <inttypes.h>

static const char *TAG = "overflow_http";

typedef struct
{
    char buf[16];
    volatile uint32_t target;
} payload_t;

/* ========== VULNERABLE FUNCTION ========== */
static void vulnerable_copy(const char *input, int len)
{
    payload_t payload;

    memset(&payload, 0, sizeof(payload));
    payload.target = 0x12345678;

    ESP_LOGI(TAG, "Before:");
    ESP_LOGI(TAG, "target = 0x%08" PRIX32, payload.target);

    // VULNERABILITY: no bounds check
    memcpy(payload.buf, input, len);

    ESP_LOGI(TAG, "After:");
    ESP_LOGI(TAG, "buf    = %.16s", payload.buf);
    ESP_LOGI(TAG, "target = 0x%08" PRIX32, payload.target);

    if (payload.target == 0x42424242)
    {
        ESP_LOGI(TAG, "SUCCESS: overwritten via HTTP input");
    }
}

/* ========== HTTP RESPONSE HANDLER ========== */
static esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    static char response_buffer[128];
    static int offset = 0;

    switch (evt->event_id)
    {
        case HTTP_EVENT_ON_DATA:
            if (!esp_http_client_is_chunked_response(evt->client))
            {
                memcpy(response_buffer + offset, evt->data, evt->data_len);
                offset += evt->data_len;
            }
            break;

        case HTTP_EVENT_ON_FINISH:
            ESP_LOGI(TAG, "Received %d bytes from server", offset);

            vulnerable_copy(response_buffer, offset);

            offset = 0; // reset for next request
            break;

        default:
            break;
    }

    return ESP_OK;
}

/* ========== DEMO ENTRY ========== */
void overflow_http_demo(void)
{
    esp_http_client_config_t config = {
        .url = "http://192.168.1.137:8000", // <-- your PC IP
        .event_handler = _http_event_handler,
        .timeout_ms = 30000,  // 30 second timeout to give us some time to enter the payload into the terminal
    };

    ESP_LOGI(TAG, "Requesting payload from server...");

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_perform(client);
    esp_http_client_cleanup(client);
}