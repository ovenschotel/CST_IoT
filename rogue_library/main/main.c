#include <stdio.h>
#include <stdlib.h>
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "smart_weather.h"

void app_main(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    
    nvs_handle_t handle;
    nvs_open("storage", NVS_READWRITE, &handle);
    nvs_set_str(handle, "api_key", "API_WeatherScanner_AEFJNQ8NH38");
    nvs_set_str(handle, "wifi_pass", "WeatherBalloon03"); 
    nvs_commit(handle);
    nvs_close(handle);

    ESP_LOGI("MainApp", "System initialized. Starting weather service...");

    smart_weather_init();

    while (1) {
        int current_temp = (rand() % 8) + 16;
        ESP_LOGI("MainApp", "The weather is currently %d°C.", current_temp);
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}