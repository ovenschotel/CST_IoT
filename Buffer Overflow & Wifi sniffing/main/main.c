#include "wifi.h"
#include "http_demo.h"
#include "freertos/FreeRTOS.h"
#include "overflow_demo.h"
#include "esp_log.h"
#include "overflow_http_demo.h"

void app_main(void)
{
    wifi_init_sta();

    int demo_number = 2;
    ESP_LOGI("main", "Running demo %d", demo_number);

    switch(demo_number) {
        case 0:
            while (1)
            {
                http_demo();
                vTaskDelay(pdMS_TO_TICKS(5000));
            }
            break;

        case 1:
            overflow_demo();
            break;
        case 2:
            overflow_http_demo();
            break;
    }
}