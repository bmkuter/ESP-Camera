/**
 * @file wifi.h
 * @brief WiFi connection module
 * 
 * Handles WiFi station mode connection and mDNS service.
 */

#ifndef WIFI_H
#define WIFI_H

#include "esp_err.h"

/**
 * @brief Initialize WiFi in station mode and connect to network
 * 
 * Uses credentials from secrets.h (WIFI_SSID and WIFI_PASSWORD).
 * Blocks until connected or connection fails.
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t wifi_init_sta(void);

/**
 * @brief Initialize mDNS service for hostname resolution
 * 
 * Sets up mDNS responder with the configured hostname.
 * Allows access via http://<hostname>.local/
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t mdns_init_service(void);

#endif // WIFI_H
