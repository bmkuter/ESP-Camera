/**
 * @file camera.h
 * @brief Camera module for XIAO ESP32S3 Sense
 * 
 * Handles camera initialization and image capture for the OV3660 sensor.
 */

#ifndef CAMERA_H
#define CAMERA_H

#include "esp_camera.h"
#include "esp_err.h"

/**
 * @brief Initialize the camera with XIAO ESP32S3 Sense configuration
 * 
 * Configures the OV3660 camera sensor with:
 * - QXGA resolution (2048x1536)
 * - JPEG format
 * - High quality (quality = 4)
 * - PSRAM frame buffers
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t camera_init(void);

/**
 * @brief Capture a fresh image from the camera
 * 
 * Returns the most recent frame from the camera buffer.
 * The caller is responsible for returning the frame buffer
 * using esp_camera_fb_return() when done.
 * 
 * @return Pointer to frame buffer on success, NULL on failure
 */
camera_fb_t* camera_capture_image(void);

#endif // CAMERA_H
