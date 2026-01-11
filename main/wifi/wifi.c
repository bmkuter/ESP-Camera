/**
 * @file wifi.c
 * @brief WiFi connection module implementation
 */

#include "wifi.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "mdns.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "secrets.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "wifi";

// mDNS hostname (access via http://growpod-camera.local)
#define MDNS_HOSTNAME "growpod-camera"

// WiFi event group
static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static int s_retry_num = 0;
#define MAX_RETRY_ATTEMPTS 5

/**
 * @brief Scan WiFi channels and report congestion
 */
static void scan_wifi_channels(void)
{
    ESP_LOGI(TAG, "Scanning WiFi channels for congestion analysis...");
    
    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active.min = 100,
        .scan_time.active.max = 300,
    };
    
    ESP_ERROR_CHECK(esp_wifi_scan_start(&scan_config, true));
    
    uint16_t ap_count = 0;
    esp_wifi_scan_get_ap_num(&ap_count);
    
    if (ap_count == 0) {
        ESP_LOGI(TAG, "No APs found");
        return;
    }
    
    wifi_ap_record_t *ap_list = malloc(sizeof(wifi_ap_record_t) * ap_count);
    if (ap_list == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for AP list");
        return;
    }
    
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&ap_count, ap_list));
    
    // Count APs per channel
    int channel_count[14] = {0};  // WiFi channels 1-13 (+ 0 for indexing)
    
    for (int i = 0; i < ap_count; i++) {
        if (ap_list[i].primary >= 1 && ap_list[i].primary <= 13) {
            channel_count[ap_list[i].primary]++;
        }
    }
    
    // Log channel congestion
    ESP_LOGI(TAG, "WiFi Channel Congestion Analysis:");
    ESP_LOGI(TAG, "  Channel  |  APs  |  Congestion");
    ESP_LOGI(TAG, "  ---------|-------|-------------");
    
    for (int ch = 1; ch <= 13; ch++) {
        if (channel_count[ch] > 0) {
            const char* level;
            if (channel_count[ch] <= 2) level = "Low";
            else if (channel_count[ch] <= 5) level = "Medium";
            else level = "High";
            
            ESP_LOGI(TAG, "     %2d    |  %2d   |  %s", ch, channel_count[ch], level);
        }
    }
    
    // Find our AP and report its channel
    for (int i = 0; i < ap_count; i++) {
        if (strcmp((char*)ap_list[i].ssid, WIFI_SSID) == 0) {
            int our_channel = ap_list[i].primary;
            ESP_LOGI(TAG, "");
            ESP_LOGI(TAG, "Your AP '%s' is on channel %d with %d other APs", 
                     WIFI_SSID, our_channel, channel_count[our_channel] - 1);
            ESP_LOGI(TAG, "  Signal strength: %d dBm", ap_list[i].rssi);
            
            // Suggest better channels if current one is congested
            if (channel_count[our_channel] > 3) {
                ESP_LOGI(TAG, "  ⚠️  Channel %d is congested!", our_channel);
                ESP_LOGI(TAG, "  💡 Consider switching your router to a less congested channel:");
                
                // Recommend least congested channels (prefer 1, 6, 11 for non-overlapping)
                int best_channel = 1;
                int min_count = channel_count[1];
                
                // Check channels 6 and 11
                if (channel_count[6] < min_count) {
                    min_count = channel_count[6];
                    best_channel = 6;
                }
                if (channel_count[11] < min_count) {
                    min_count = channel_count[11];
                    best_channel = 11;
                }
                
                if (best_channel != our_channel) {
                    ESP_LOGI(TAG, "     Recommended: Channel %d (%d APs)", best_channel, min_count);
                }
            } else {
                ESP_LOGI(TAG, "  ✓ Channel %d looks good!", our_channel);
            }
            break;
        }
    }
    
    free(ap_list);
    ESP_LOGI(TAG, "");
}

/**
 * @brief WiFi event handler
 */
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < MAX_RETRY_ATTEMPTS) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "Retry connecting to WiFi... (%d/%d)", s_retry_num, MAX_RETRY_ATTEMPTS);
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            ESP_LOGE(TAG, "Failed to connect to WiFi after %d attempts", MAX_RETRY_ATTEMPTS);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP Address: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

esp_err_t wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASSWORD,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    
    // Disable power saving for maximum performance and low latency
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    ESP_LOGI(TAG, "WiFi power saving disabled for maximum performance");
    
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Connecting to WiFi SSID: %s", WIFI_SSID);

    // Wait for connection
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected to WiFi successfully");
        
        // Scan and analyze WiFi channel congestion
        scan_wifi_channels();
        
        return ESP_OK;
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGE(TAG, "Failed to connect to WiFi");
        return ESP_FAIL;
    }

    ESP_LOGE(TAG, "Unexpected WiFi connection error");
    return ESP_FAIL;
}

esp_err_t mdns_init_service(void)
{
    // Initialize mDNS
    esp_err_t err = mdns_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mDNS Init failed: %d", err);
        return err;
    }

    // Set hostname
    mdns_hostname_set(MDNS_HOSTNAME);
    
    // Set instance name
    mdns_instance_name_set("GrowPod ESP32-S3 Camera");

    // Add HTTP service
    mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
    
    ESP_LOGI(TAG, "mDNS service started");
    ESP_LOGI(TAG, "Access camera at: http://%s.local/", MDNS_HOSTNAME);
    
    // Give mDNS time to announce the service on the network
    ESP_LOGI(TAG, "Waiting for mDNS announcement...");
    vTaskDelay(pdMS_TO_TICKS(2000));  // 2 second delay
    ESP_LOGI(TAG, "mDNS ready");
    
    return ESP_OK;
}
