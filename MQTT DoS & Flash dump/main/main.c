#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "mqtt_client.h"
#include "esp_wifi.h"

#include "nvs_flash.h"

#define PIR_GPIO GPIO_NUM_4

static const char *TAG = "PIR_SENSOR";
static esp_mqtt_client_handle_t mqtt_client;

static void wifi_init(void) {
    nvs_flash_init();
    esp_netif_init();
    esp_event_loop_create_default();

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = "niet voor jou",
            .password = "7zh0wnqa2m",
        },
    };

    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_start();
    esp_wifi_connect();
}

static void mqtt_init(void) {
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = "mqtt://192.168.1.181",
    };

    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_start(mqtt_client);
}

void app_main(void) {
    wifi_init();
    vTaskDelay(pdMS_TO_TICKS(5000)); // wait for connection
    mqtt_init();

    gpio_reset_pin(PIR_GPIO);
    gpio_set_direction(PIR_GPIO, GPIO_MODE_INPUT);
    gpio_set_pull_mode(PIR_GPIO, GPIO_PULLDOWN_ONLY);

    ESP_LOGI(TAG, "PIR Monitoring started on GPIO %d", PIR_GPIO);

    for (int i = 10; i > 0; i--) {
        printf("Sensor stabilizing... %d seconds left\n", i);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    
    ESP_LOGI(TAG, "System LIVE. Wave your hand!");

    while (1) {
        if (gpio_get_level(PIR_GPIO) == 1) {
            ESP_LOGW(TAG, "!!! MOTION DETECTED !!!");

            esp_mqtt_client_publish(mqtt_client, "pir/sensor", "motion_detected", 0, 1, 0);

            while(gpio_get_level(PIR_GPIO) == 1) {
                vTaskDelay(pdMS_TO_TICKS(100));
            }

            ESP_LOGI(TAG, "Motion ended.");

            esp_mqtt_client_publish(mqtt_client, "pir/sensor", "motion_ended", 0, 1, 0);
        }
        vTaskDelay(pdMS_TO_TICKS(10)); 
    }
}