#include "overflow_demo.h"
#include "esp_log.h"
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>

static const char *TAG = "overflow";

/*
    Using a struct makes the layout predictable:
    payload.buf is followed by payload.target in memory.
*/
typedef struct
{
    char buf[16];
    volatile uint32_t target;
} payload_t;

static void hexdump_bytes(const uint8_t *data, size_t len)
{
    for (size_t i = 0; i < len; i++)
    {
        printf("%02X ", data[i]);
    }
    printf("\n");
}

void overflow_demo(void)
{
    payload_t payload;

    memset(&payload, 0, sizeof(payload));
    payload.target = 0x12345678;

    ESP_LOGI(TAG, "Before overflow:");
    ESP_LOGI(TAG, "target = 0x%08" PRIX32, payload.target);

    /*
        16 bytes fill buf exactly.
        The next 4 bytes overwrite target.
        On ESP32 (little-endian), target becomes 0x42424242.

        We kunnen deze vervolgens sturen vanaf een http server
    */
    const char attack[] = "AAAAAAAAAAAAAAAABBBB";

    ESP_LOGI(TAG, "Attack length = %d", (int)strlen(attack));
    ESP_LOGI(TAG, "Raw attack bytes:");
    hexdump_bytes((const uint8_t *)attack, strlen(attack));

    // Vulnerability: no bounds check
    memcpy(payload.buf, attack, strlen(attack));

    ESP_LOGI(TAG, "After overflow:");
    ESP_LOGI(TAG, "buf    = %.16s", payload.buf);
    ESP_LOGI(TAG, "target = 0x%08" PRIX32, payload.target);

    if (payload.target == 0x42424242)
    {
        ESP_LOGI(TAG, "SUCCESS: target overwritten exactly as intended.");
    }
    else
    {
        ESP_LOGE(TAG, "Unexpected target value.");
    }
}

void overflow_demo_safe(void)
{
    payload_t payload;

    memset(&payload, 0, sizeof(payload));
    payload.target = 0x12345678;

    const char attack[] = "AAAAAAAAAAAAAAAABBBB";

    ESP_LOGI(TAG, "Before safe copy:");
    ESP_LOGI(TAG, "target = 0x%08" PRIX32, payload.target);

    // Safe copy: only fill buf, never overwrite target
    size_t copy_len = strlen(attack);
    if (copy_len > sizeof(payload.buf))
    {
        copy_len = sizeof(payload.buf);
    }

    memcpy(payload.buf, attack, copy_len);

    ESP_LOGI(TAG, "After safe copy:");
    ESP_LOGI(TAG, "buf    = %.16s", payload.buf);
    ESP_LOGI(TAG, "target = 0x%08" PRIX32, payload.target);

    if (payload.target == 0x12345678)
    {
        ESP_LOGI(TAG, "SAFE: target unchanged.");
    }
    else
    {
        ESP_LOGE(TAG, "Something still overwrote target.");
    }
}