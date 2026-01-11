/**
 * @file camera.c
 * @brief Camera module implementation for XIAO ESP32S3 Sense
 */

#include "camera.h"
#include "esp_log.h"

static const char *TAG = "camera";

// XIAO ESP32S3 Sense camera pin definitions
#define CAMERA_PIN_PWDN    -1
#define CAMERA_PIN_RESET   -1
#define CAMERA_PIN_XCLK    10
#define CAMERA_PIN_SIOD    40
#define CAMERA_PIN_SIOC    39

#define CAMERA_PIN_D7      48
#define CAMERA_PIN_D6      11
#define CAMERA_PIN_D5      12
#define CAMERA_PIN_D4      14
#define CAMERA_PIN_D3      16
#define CAMERA_PIN_D2      18
#define CAMERA_PIN_D1      17
#define CAMERA_PIN_D0      15
#define CAMERA_PIN_VSYNC   38
#define CAMERA_PIN_HREF    47
#define CAMERA_PIN_PCLK    13

esp_err_t camera_init(void)
{
    camera_config_t config = {
        .pin_pwdn = CAMERA_PIN_PWDN,
        .pin_reset = CAMERA_PIN_RESET,
        .pin_xclk = CAMERA_PIN_XCLK,
        .pin_sccb_sda = CAMERA_PIN_SIOD,
        .pin_sccb_scl = CAMERA_PIN_SIOC,

        .pin_d7 = CAMERA_PIN_D7,
        .pin_d6 = CAMERA_PIN_D6,
        .pin_d5 = CAMERA_PIN_D5,
        .pin_d4 = CAMERA_PIN_D4,
        .pin_d3 = CAMERA_PIN_D3,
        .pin_d2 = CAMERA_PIN_D2,
        .pin_d1 = CAMERA_PIN_D1,
        .pin_d0 = CAMERA_PIN_D0,
        .pin_vsync = CAMERA_PIN_VSYNC,
        .pin_href = CAMERA_PIN_HREF,
        .pin_pclk = CAMERA_PIN_PCLK,

        .xclk_freq_hz = 20000000,           // 20MHz XCLK
        .ledc_timer = LEDC_TIMER_0,
        .ledc_channel = LEDC_CHANNEL_0,

        .pixel_format = PIXFORMAT_JPEG,     // JPEG format for easy transmission
        .frame_size = FRAMESIZE_QXGA,       // 2048x1536 - Maximum quality for OV3660!
                                             // For faster capture, try: FRAMESIZE_UXGA (1600x1200)
                                             // or FRAMESIZE_SXGA (1280x1024)
        .jpeg_quality = 4,                  // 0-63, lower means higher quality (4 = excellent)
                                             // For smaller/faster files, try: 8-12
        .fb_count = 1,                      // Single frame buffer for immediate fresh frames
        .fb_location = CAMERA_FB_IN_PSRAM,  // Explicitly use PSRAM
        .grab_mode = CAMERA_GRAB_LATEST     // Always grab latest frame
    };

    // Initialize the camera
    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Camera init failed with error 0x%x", err);
        return err;
    }

    // Get camera sensor
    sensor_t *s = esp_camera_sensor_get();
    if (s == NULL) {
        ESP_LOGE(TAG, "Failed to get camera sensor");
        return ESP_FAIL;
    }

    // Optional: Adjust sensor settings for better image quality
    s->set_vflip(s, 1);        // Flip vertically
    s->set_hmirror(s, 0);      // Mirror horizontally
    
    ESP_LOGI(TAG, "Camera initialized successfully");
    return ESP_OK;
}

camera_fb_t* camera_capture_image(void)
{
    // Discard the first frame to ensure we get a fresh image
    // This solves the "1 frame lag" issue where you see the previous scene
    camera_fb_t *fb = esp_camera_fb_get();
    if (fb) {
        esp_camera_fb_return(fb);  // Return the stale frame
    }
    
    // Now get a fresh frame
    fb = esp_camera_fb_get();
    if (!fb) {
        ESP_LOGE(TAG, "Camera capture failed");
        return NULL;
    }
    
    ESP_LOGI(TAG, "Image captured: %zu bytes, %dx%d", 
             fb->len, fb->width, fb->height);
    return fb;
}
