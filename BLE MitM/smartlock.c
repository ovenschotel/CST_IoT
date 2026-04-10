#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/gpio.h"

#include "esp_bt.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_bt_main.h"
#include "esp_gatt_common_api.h"

#define TAG "SMARTLOCK_DEMO"

// LED pins
#define LED_GPIO_1 GPIO_NUM_4
#define LED_GPIO_2 GPIO_NUM_5

// BLE UUIDs
#define SERVICE_UUID 0x1234
#define CHAR_UUID    0xABCD

// Advertising settings
static esp_ble_adv_params_t adv_params = {
    .adv_int_min        = 0x20,
    .adv_int_max        = 0x40,
    .adv_type           = ADV_TYPE_IND,
    .own_addr_type      = BLE_ADDR_TYPE_PUBLIC,
    .channel_map        = ADV_CHNL_ALL,
    .adv_filter_policy  = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

// Command handler
static void handle_command(const uint8_t *data, uint16_t len) {
    char cmd[20] = {0};
    memcpy(cmd, data, len);

    ESP_LOGI(TAG, "Received: %s", cmd);

    if (strcmp(cmd, "LOCK") == 0) {
        gpio_set_level(LED_GPIO_1, 1);
        gpio_set_level(LED_GPIO_2, 0);
        ESP_LOGI(TAG, "LOCK -> LED1 ON");
    } 
    else if (strcmp(cmd, "UNLOCK") == 0) {
        gpio_set_level(LED_GPIO_1, 0);
        gpio_set_level(LED_GPIO_2, 1);
        ESP_LOGI(TAG, "UNLOCK -> LED2 ON");
    } else {
        ESP_LOGW(TAG, "Unknown command");
    }
}

// GAP callback for advertising status
static void gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param){
    if(event == ESP_GAP_BLE_ADV_START_COMPLETE_EVT){
        if(param->adv_start_cmpl.status == ESP_BT_STATUS_SUCCESS){
            ESP_LOGI(TAG, "Advertising started successfully");
        } else {
            ESP_LOGE(TAG, "Failed to start advertising");
        }
    }
}

// GATT events
static void gatts_event_handler(esp_gatts_cb_event_t event,
                                esp_gatt_if_t gatts_if,
                                esp_ble_gatts_cb_param_t *param) {

    switch (event) {

    case ESP_GATTS_REG_EVT:
        ESP_LOGI(TAG, "REGISTERED");

        esp_ble_gap_register_callback(gap_cb);
        esp_ble_gap_set_device_name("SmartLock");
        esp_ble_gap_start_advertising(&adv_params);

        esp_gatt_srvc_id_t service_id = {
            .is_primary = true,
            .id.inst_id = 0,
            .id.uuid.len = ESP_UUID_LEN_16,
            .id.uuid.uuid.uuid16 = SERVICE_UUID,
        };

        esp_ble_gatts_create_service(gatts_if, &service_id, 4);
        break;

    case ESP_GATTS_CREATE_EVT:
        ESP_LOGI(TAG, "SERVICE CREATED");

        esp_ble_gatts_start_service(param->create.service_handle);

        esp_bt_uuid_t char_uuid = {
            .len = ESP_UUID_LEN_16,
            .uuid.uuid16 = CHAR_UUID,
        };

        // 🔹 Windows-compatibel: alleen WRITE (geen WRITE_NR)
        esp_ble_gatts_add_char(param->create.service_handle,
                               &char_uuid,
                               ESP_GATT_PERM_WRITE,
                               ESP_GATT_CHAR_PROP_BIT_WRITE,  // verwijder WRITE_NR
                               NULL,
                               NULL);
        break;

    case ESP_GATTS_WRITE_EVT:
        ESP_LOGI(TAG, "WRITE EVENT, LEN=%d", param->write.len);
        printf("Data HEX: ");
        for(int i=0;i<param->write.len;i++) printf("%02X ", param->write.value[i]);
        printf("\n");
        handle_command(param->write.value, param->write.len);
        break;

    default:
        break;
    }
}

// MAIN
void app_main(void) {

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    // LED setup
    gpio_reset_pin(LED_GPIO_1);
    gpio_set_direction(LED_GPIO_1, GPIO_MODE_OUTPUT);

    gpio_reset_pin(LED_GPIO_2);
    gpio_set_direction(LED_GPIO_2, GPIO_MODE_OUTPUT);

    // Test LEDs
    gpio_set_level(LED_GPIO_1, 1);
    gpio_set_level(LED_GPIO_2, 1);
    vTaskDelay(2000 / portTICK_PERIOD_MS);
    gpio_set_level(LED_GPIO_1, 0);
    gpio_set_level(LED_GPIO_2, 0);

    // Bluetooth init
    esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    esp_bt_controller_init(&bt_cfg);
    esp_bt_controller_enable(ESP_BT_MODE_BLE);

    esp_bluedroid_init();
    esp_bluedroid_enable();

    // GATT server
    esp_ble_gatts_register_callback(gatts_event_handler);
    esp_ble_gatts_app_register(0);

    ESP_LOGI(TAG, "SmartLock BLE demo started");
}