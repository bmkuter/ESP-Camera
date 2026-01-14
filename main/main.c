/**
 * @file main.c
 * @brief GrowPod ESP32-S3 Camera Application
 * 
 * Main application entry point that initializes camera, WiFi, mDNS,
 * and HTTP web server for remote image capture.
 */

#include <stdio.h>
#include "esp_log.h"
#include "esp_system.h"
#include "esp_psram.h"
#include "nvs_flash.h"
#include "camera/camera.h"
#include "wifi/wifi.h"
#include "web_server/web_server.h"
#include "settings/settings.h"

static const char *TAG = "main";

void app_main(void)
{
    ESP_LOGI(TAG, "GrowPod ESP32-S3 Camera starting...");
    
    // Initialize NVS (required for WiFi and settings)
    ESP_LOGI(TAG, "Initializing NVS...");
    if (settings_init() != ESP_OK) {
        ESP_LOGE(TAG, "NVS initialization failed!");
        return;
    }
    ESP_LOGI(TAG, "NVS initialized");
    
    // Check PSRAM
    if (esp_psram_is_initialized()) {
        ESP_LOGI(TAG, "PSRAM initialized successfully");
        ESP_LOGI(TAG, "PSRAM size: %d bytes", esp_psram_get_size());
    } else {
        ESP_LOGE(TAG, "PSRAM not initialized!");
    }
    
    // Initialize camera
    ESP_LOGI(TAG, "Initializing camera...");
    if (camera_init() != ESP_OK) {
        ESP_LOGE(TAG, "Camera initialization failed!");
        return;
    }
    ESP_LOGI(TAG, "Camera initialized successfully");
    
    // Load and apply saved settings
    ESP_LOGI(TAG, "Loading camera settings...");
    camera_settings_t settings;
    esp_err_t err = settings_load(&settings);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Applying saved settings to camera");
        settings_apply_to_camera(&settings);
    } else {
        ESP_LOGI(TAG, "No saved settings found, using defaults");
        settings_get_defaults(&settings);
        settings_apply_to_camera(&settings);
        // Save the defaults so they're available next time
        settings_save(&settings);
    }
    
    // Initialize WiFi
    ESP_LOGI(TAG, "Initializing WiFi...");
    if (wifi_init_sta() != ESP_OK) {
        ESP_LOGE(TAG, "WiFi initialization failed!");
        return;
    }
    ESP_LOGI(TAG, "WiFi connected successfully");
    
    // Initialize mDNS
    ESP_LOGI(TAG, "Initializing mDNS...");
    if (mdns_init_service() != ESP_OK) {
        ESP_LOGE(TAG, "mDNS initialization failed!");
        return;
    }
    ESP_LOGI(TAG, "mDNS service started");
    
    // Start web server
    ESP_LOGI(TAG, "Starting web server...");
    httpd_handle_t server = start_webserver();
    if (server == NULL) {
        ESP_LOGE(TAG, "Failed to start web server!");
        return;
    }
    
    ESP_LOGI(TAG, "==============================================");
    ESP_LOGI(TAG, "GrowPod Camera ready!");
    ESP_LOGI(TAG, "Access via: http://growpod-camera.local/");
    ESP_LOGI(TAG, "==============================================");
}
