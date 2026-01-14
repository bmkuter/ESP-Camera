/**
 * @file settings.c
 * @brief Camera settings persistence implementation using NVS
 */

#include "settings/settings.h"
#include "esp_camera.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"

static const char *TAG = "settings";

// NVS namespace for camera settings
#define NVS_NAMESPACE "camera"
#define NVS_KEY "settings"

// Current settings version (increment when structure changes)
#define SETTINGS_VERSION 1

esp_err_t settings_init(void)
{
    ESP_LOGI(TAG, "Initializing settings storage");
    
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // NVS partition was truncated and needs to be erased
        ESP_LOGW(TAG, "NVS partition needs to be erased, erasing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize NVS: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "Settings storage initialized");
    return ESP_OK;
}

void settings_get_defaults(camera_settings_t *settings)
{
    if (settings == NULL) {
        return;
    }
    
    // Set default values matching camera_init() in camera.c
    settings->version = SETTINGS_VERSION;
    
    // Exposure defaults (auto mode)
    settings->aec = 1;          // Auto exposure enabled
    settings->aec_value = 300;  // Default manual value
    settings->ae_level = 0;     // No compensation
    
    // Gain defaults (auto mode)
    settings->agc = 1;          // Auto gain enabled
    settings->agc_gain = 0;     // Default manual value
    
    // Image quality defaults
    settings->quality = 4;      // Excellent quality (0-63, lower=better)
    settings->framesize = 19;   // FRAMESIZE_QXGA (2048x1536)
    
    // Image adjustments (neutral)
    settings->brightness = 0;
    settings->contrast = 0;
    settings->saturation = 0;
    settings->sharpness = 0;
    
    // Other settings
    settings->awb = 1;          // Auto white balance enabled
    settings->hmirror = 0;      // No horizontal mirror
    settings->vflip = 1;        // Vertical flip enabled (from camera_init)
    
    ESP_LOGI(TAG, "Default settings initialized");
}

esp_err_t settings_load(camera_settings_t *settings)
{
    if (settings == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    nvs_handle_t nvs_handle;
    esp_err_t err;
    
    // Open NVS
    err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGI(TAG, "No saved settings found, using defaults");
        } else {
            ESP_LOGE(TAG, "Error opening NVS handle: %s", esp_err_to_name(err));
        }
        return err;
    }
    
    // Read settings blob
    size_t required_size = sizeof(camera_settings_t);
    err = nvs_get_blob(nvs_handle, NVS_KEY, settings, &required_size);
    
    nvs_close(nvs_handle);
    
    if (err != ESP_OK) {
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGI(TAG, "No saved settings found");
        } else {
            ESP_LOGE(TAG, "Error reading settings: %s", esp_err_to_name(err));
        }
        return err;
    }
    
    // Check version compatibility
    if (settings->version != SETTINGS_VERSION) {
        ESP_LOGW(TAG, "Settings version mismatch (saved: %d, current: %d), using defaults",
                 settings->version, SETTINGS_VERSION);
        return ESP_ERR_INVALID_VERSION;
    }
    
    ESP_LOGI(TAG, "Settings loaded from NVS");
    ESP_LOGI(TAG, "  Resolution: framesize=%d, quality=%d", settings->framesize, settings->quality);
    ESP_LOGI(TAG, "  Exposure: aec=%d, aec_value=%d, ae_level=%d", 
             settings->aec, settings->aec_value, settings->ae_level);
    ESP_LOGI(TAG, "  Gain: agc=%d, agc_gain=%d", settings->agc, settings->agc_gain);
    
    return ESP_OK;
}

esp_err_t settings_save(const camera_settings_t *settings)
{
    if (settings == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    nvs_handle_t nvs_handle;
    esp_err_t err;
    
    // Open NVS for writing
    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error opening NVS handle: %s", esp_err_to_name(err));
        return err;
    }
    
    // Write settings blob
    err = nvs_set_blob(nvs_handle, NVS_KEY, settings, sizeof(camera_settings_t));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error writing settings: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }
    
    // Commit changes
    err = nvs_commit(nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error committing settings: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }
    
    nvs_close(nvs_handle);
    
    ESP_LOGI(TAG, "Settings saved to NVS");
    return ESP_OK;
}

esp_err_t settings_apply_to_camera(const camera_settings_t *settings)
{
    if (settings == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    sensor_t *s = esp_camera_sensor_get();
    if (s == NULL) {
        ESP_LOGE(TAG, "Failed to get camera sensor");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Applying saved settings to camera");
    
    // Apply resolution and quality
    if (s->set_framesize) {
        s->set_framesize(s, (framesize_t)settings->framesize);
    }
    if (s->set_quality) {
        s->set_quality(s, settings->quality);
    }
    
    // Apply exposure settings
    if (s->set_exposure_ctrl) {
        s->set_exposure_ctrl(s, settings->aec);
    }
    if (s->set_aec_value) {
        s->set_aec_value(s, settings->aec_value);
    }
    if (s->set_ae_level) {
        s->set_ae_level(s, settings->ae_level);
    }
    
    // Apply gain settings
    if (s->set_gain_ctrl) {
        s->set_gain_ctrl(s, settings->agc);
    }
    if (s->set_agc_gain) {
        s->set_agc_gain(s, settings->agc_gain);
    }
    
    // Apply image adjustments (with null checks)
    if (s->set_brightness) {
        s->set_brightness(s, settings->brightness);
    }
    if (s->set_contrast) {
        s->set_contrast(s, settings->contrast);
    }
    if (s->set_saturation) {
        s->set_saturation(s, settings->saturation);
    }
    if (s->set_sharpness) {
        s->set_sharpness(s, settings->sharpness);
    }
    
    // Apply other settings
    if (s->set_whitebal) {
        s->set_whitebal(s, settings->awb);
    }
    if (s->set_hmirror) {
        s->set_hmirror(s, settings->hmirror);
    }
    if (s->set_vflip) {
        s->set_vflip(s, settings->vflip);
    }
    
    ESP_LOGI(TAG, "Settings applied to camera");
    return ESP_OK;
}

esp_err_t settings_read_from_camera(camera_settings_t *settings)
{
    if (settings == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    sensor_t *s = esp_camera_sensor_get();
    if (s == NULL) {
        ESP_LOGE(TAG, "Failed to get camera sensor");
        return ESP_FAIL;
    }
    
    // Read current camera status
    settings->version = SETTINGS_VERSION;
    settings->framesize = s->status.framesize;
    settings->quality = s->status.quality;
    settings->aec = s->status.aec;
    settings->aec_value = s->status.aec_value;
    settings->ae_level = s->status.ae_level;
    settings->agc = s->status.agc;
    settings->agc_gain = s->status.agc_gain;
    settings->brightness = s->status.brightness;
    settings->contrast = s->status.contrast;
    settings->saturation = s->status.saturation;
    settings->sharpness = s->status.sharpness;
    settings->awb = s->status.awb;
    settings->hmirror = s->status.hmirror;
    settings->vflip = s->status.vflip;
    
    return ESP_OK;
}
