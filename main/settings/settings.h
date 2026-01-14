/**
 * @file settings.h
 * @brief Camera settings persistence using NVS (Non-Volatile Storage)
 */

#ifndef SETTINGS_H
#define SETTINGS_H

#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Camera settings structure for persistent storage
 */
typedef struct {
    // Exposure settings
    uint8_t aec;            // Auto exposure control (0=manual, 1=auto)
    uint16_t aec_value;     // Manual exposure value (0-1200)
    int8_t ae_level;        // Exposure compensation (-2 to +2)
    
    // Gain settings
    uint8_t agc;            // Auto gain control (0=manual, 1=auto)
    uint8_t agc_gain;       // Manual gain value (0-30)
    
    // Image quality
    uint8_t quality;        // JPEG quality (0-63, lower=better)
    uint8_t framesize;      // Frame size/resolution
    
    // Image adjustments
    int8_t brightness;      // Brightness (-2 to +2)
    int8_t contrast;        // Contrast (-2 to +2)
    int8_t saturation;      // Saturation (-2 to +2)
    int8_t sharpness;       // Sharpness (-2 to +2)
    
    // Other settings
    uint8_t awb;            // Auto white balance
    uint8_t hmirror;        // Horizontal mirror
    uint8_t vflip;          // Vertical flip
    
    // Settings version for future compatibility
    uint8_t version;
} camera_settings_t;

/**
 * @brief Initialize NVS and load saved settings
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t settings_init(void);

/**
 * @brief Load camera settings from NVS
 * 
 * @param settings Pointer to settings structure to populate
 * @return ESP_OK on success, ESP_ERR_NVS_NOT_FOUND if no settings saved
 */
esp_err_t settings_load(camera_settings_t *settings);

/**
 * @brief Save camera settings to NVS
 * 
 * @param settings Pointer to settings structure to save
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t settings_save(const camera_settings_t *settings);

/**
 * @brief Get default camera settings
 * 
 * @param settings Pointer to settings structure to populate with defaults
 */
void settings_get_defaults(camera_settings_t *settings);

/**
 * @brief Apply saved settings to camera sensor
 * 
 * @param settings Pointer to settings structure to apply
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t settings_apply_to_camera(const camera_settings_t *settings);

/**
 * @brief Read current settings from camera sensor
 * 
 * @param settings Pointer to settings structure to populate
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t settings_read_from_camera(camera_settings_t *settings);

#ifdef __cplusplus
}
#endif

#endif // SETTINGS_H
