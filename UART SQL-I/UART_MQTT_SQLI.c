#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "mqtt_client.h"

#define UART_PORT UART_NUM_2
#define UART_TX_PIN 17
#define UART_RX_PIN 16
#define BUF_SIZE 1024

static const char *TAG = "MQTT_CLI";

// Hardcoded Wi-Fi & MQTT settings
#define WIFI_SSID      "Hier_Je_SSID"
#define WIFI_PASS      "Hier_Je_Password"
#define BROKER_URL     "mqtt://<IP_Van_De_MQTT"

// Global MQTT handle
static esp_mqtt_client_handle_t mqtt_client = NULL;

// UART write helper
static void uart_write(const char *str) {
    uart_write_bytes(UART_PORT, str, strlen(str));
}

// ================= MQTT =================
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = event_data;
    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            uart_write("\r\n[MQTT] Connected!\r\n> ");
            break;
        case MQTT_EVENT_DISCONNECTED:
            uart_write("\r\n[MQTT] Disconnected\r\n> ");
            break;
        case MQTT_EVENT_DATA:
            uart_write("\r\n[MQTT] Data received\r\n> ");
            break;
        case MQTT_EVENT_ERROR:
            uart_write("\r\n[MQTT] Error\r\n> ");
            break;
        default:
            break;
    }
}

// Start MQTT
static void mqtt_start(void) {
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = BROKER_URL,
    };
    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(mqtt_client);
}

// ================= Wi-Fi =================
static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
        ESP_LOGI(TAG, "Retry connecting to AP...");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP:" IPSTR, IP2STR(&event->ip_info.ip));
        // Start MQTT after Wi-Fi ready
        mqtt_start();
    }
}

static void wifi_init_sta(void) {
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    esp_event_handler_instance_t any_id;
    esp_event_handler_instance_t got_ip;
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &any_id);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, &got_ip);

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
        },
    };
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_start();
}

// ================= UART CLI =================
static void uart_init(void) {
    uart_config_t cfg = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
    };
    uart_driver_install(UART_PORT, BUF_SIZE * 2, 0, 0, NULL, 0);
    uart_param_config(UART_PORT, &cfg);
    uart_set_pin(UART_PORT, UART_TX_PIN, UART_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
}

// CLI task
static void cli_task(void *arg) {
    uint8_t data[BUF_SIZE];
    char line[256];
    int idx = 0;

    uart_write("\r\nESP32 CLI ready. Type 'help'\r\n> ");

    while (1) {
        int len = uart_read_bytes(UART_PORT, data, BUF_SIZE, pdMS_TO_TICKS(100));
        if (len > 0) {
            for (int i = 0; i < len; i++) {
                char c = data[i];
                uart_write_bytes(UART_PORT, &c, 1);

                if (c == '\r' || c == '\n') {
                    line[idx] = '\0';

                    // ===== COMMAND HANDLER =====
                    if (strncmp(line, "command ", 8) == 0) {
                        if (mqtt_client) {
                            esp_mqtt_client_publish(mqtt_client, "esp32/data", line + 8, 0, 1, 0);
                            uart_write("\r\n[MQTT] Sent\r\n> ");
                        } else {
                            uart_write("\r\n[ERR] MQTT not connected yet\r\n> ");
                        }
                    }
                    else if (strcmp(line, "help") == 0) {
                        uart_write("\r\nCommands:\r\nhelp\r\ncommand <msg>\r\n> ");
                    }
                    else {
                        uart_write("\r\nUnknown command\r\n> ");
                    }

                    idx = 0;
                }
                else if (idx < sizeof(line) - 1) {
                    line[idx++] = c;
                }
            }
        }
    }
}

// ================= MAIN =================
void app_main(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "Starting...");
    uart_init();
    wifi_init_sta();

    xTaskCreate(cli_task, "cli_task", 4096, NULL, 10, NULL);
}