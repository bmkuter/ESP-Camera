/**
 * @file web_server.c
 * @brief HTTP web server implementation with camera endpoints
 */

#include "web_server/web_server.h"
#include "camera/camera.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_camera.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "web_server";

/**
 * @brief Root page handler - display status and links
 */
static esp_err_t root_get_handler(httpd_req_t *req)
{
    const char* html = 
        "<!DOCTYPE html>"
        "<html>"
        "<head>"
        "<meta charset=\"UTF-8\">"
        "<title>GrowPod Camera</title>"
        "<style>"
        "body { font-family: Arial, sans-serif; margin: 40px; background: #f5f5f5; }"
        "h1 { color: #333; }"
        ".container { background: white; padding: 20px; border-radius: 8px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); max-width: 600px; }"
        ".button { display: inline-block; padding: 10px 20px; margin: 5px; background: #4CAF50; color: white; text-decoration: none; border-radius: 4px; }"
        ".button:hover { background: #45a049; }"
        ".status { background: #e8f5e9; padding: 10px; border-radius: 4px; margin: 10px 0; }"
        "</style>"
        "</head>"
        "<body>"
        "<div class=\"container\">"
        "<h1>GrowPod ESP32-S3 Camera</h1>"
        "<div class=\"status\">"
        "<p><strong>Status:</strong> Ready</p>"
        "<p><strong>Resolution:</strong> QXGA (2048x1536)</p>"
        "<p><strong>Format:</strong> JPEG</p>"
        "</div>"
        "<p><a class=\"button\" href=\"/preview\">Live Preview</a></p>"
        "<p><a class=\"button\" href=\"/settings\">Camera Settings</a></p>"
        "<p><a class=\"button\" href=\"/capture\">Capture Image</a></p>"
        "<p><a class=\"button\" href=\"/status\">Get Status (JSON)</a></p>"
        "</div>"
        "</body>"
        "</html>";
    
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, html, strlen(html));
    return ESP_OK;
}

/**
 * @brief Simple preview page - live stream with capture button
 */
static esp_err_t preview_get_handler(httpd_req_t *req)
{
    const char* html = 
        "<!DOCTYPE html>"
        "<html>"
        "<head>"
        "<meta charset=\"UTF-8\">"
        "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">"
        "<title>Live Preview - GrowPod</title>"
        "<style>"
        "body { font-family: Arial, sans-serif; margin: 20px; background: #f5f5f5; text-align: center; }"
        "h1 { color: #333; margin-bottom: 10px; }"
        ".container { background: white; padding: 20px; border-radius: 8px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); max-width: 700px; margin: 0 auto; }"
        ".video-container { margin: 20px auto; border-radius: 4px; overflow: hidden; max-width: 640px; }"
        "img { width: 100%; height: auto; display: block; }"
        ".button { display: inline-block; padding: 12px 24px; margin: 8px; background: #4CAF50; color: white; text-decoration: none; border-radius: 4px; border: none; font-size: 16px; cursor: pointer; }"
        ".button:hover { background: #45a049; }"
        ".button.secondary { background: #2196F3; }"
        ".button.secondary:hover { background: #0b7dda; }"
        ".status-text { color: #666; margin: 10px 0; font-style: italic; }"
        "</style>"
        "</head>"
        "<body>"
        "<div class=\"container\">"
        "<h1>Live Camera Preview</h1>"
        "<p class=\"status-text\">Streaming at VGA (640x480) resolution</p>"
        "<div class=\"video-container\">"
        "<img id=\"stream\" src=\"/stream?quality=10\" alt=\"Loading stream...\">"
        "</div>"
        "<div>"
        "<button class=\"button\" onclick=\"captureHighRes()\">Capture High-Res Image</button>"
        "</div>"
        "<p class=\"status-text\" id=\"status\"></p>"
        "<p><a class=\"button secondary\" href=\"/\">Back to Home</a></p>"
        "</div>"
        "<script>"
        "function captureHighRes() {"
        "  document.getElementById('status').textContent = 'Capturing QXGA image...';"
        "  window.open('/capture', '_blank');"
        "  setTimeout(function() {"
        "    document.getElementById('status').textContent = 'Image opened in new tab';"
        "  }, 1000);"
        "}"
        "document.getElementById('stream').onerror = function() {"
        "  setTimeout(function() {"
        "    document.getElementById('stream').src = '/stream?quality=10&t=' + new Date().getTime();"
        "  }, 2000);"
        "};"
        "</script>"
        "</body>"
        "</html>";
    
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, html, strlen(html));
    return ESP_OK;
}

/**
 * @brief Camera settings page with current value retrieval
 */
static esp_err_t settings_get_handler(httpd_req_t *req)
{
    const char* html = 
        "<!DOCTYPE html>"
        "<html>"
        "<head>"
        "<meta charset=\"UTF-8\">"
        "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">"
        "<title>Camera Settings - GrowPod</title>"
        "<style>"
        "body { font-family: Arial, sans-serif; margin: 20px; background: #f5f5f5; }"
        "h1 { color: #333; text-align: center; }"
        ".container { background: white; padding: 30px; border-radius: 8px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); max-width: 600px; margin: 0 auto; }"
        ".control-group { margin: 20px 0; }"
        ".control-group label { display: block; font-weight: bold; margin-bottom: 8px; color: #555; }"
        ".control-group select, .control-group input[type='range'] { width: 100%; padding: 8px; font-size: 14px; }"
        ".control-group input[type='range'] { -webkit-appearance: none; height: 8px; border-radius: 5px; background: #ddd; outline: none; }"
        ".control-group input[type='range']::-webkit-slider-thumb { -webkit-appearance: none; appearance: none; width: 20px; height: 20px; border-radius: 50%; background: #4CAF50; cursor: pointer; }"
        ".value-display { display: inline-block; margin-left: 10px; font-weight: bold; min-width: 50px; color: #4CAF50; }"
        ".button { display: inline-block; padding: 12px 24px; margin: 8px; background: #4CAF50; color: white; text-decoration: none; border-radius: 4px; border: none; font-size: 16px; cursor: pointer; }"
        ".button:hover { background: #45a049; }"
        ".button.secondary { background: #2196F3; }"
        ".button.secondary:hover { background: #0b7dda; }"
        ".status-text { text-align: center; color: #666; margin: 15px 0; font-style: italic; }"
        ".button-group { text-align: center; margin-top: 30px; }"
        "hr { margin: 30px 0; border: none; border-top: 1px solid #ddd; }"
        "</style>"
        "</head>"
        "<body>"
        "<div class=\"container\">"
        "<h1>Camera Settings</h1>"
        "<p class=\"status-text\" id=\"loading\">Loading current settings...</p>"
        "<div id=\"settings\" style=\"display:none;\">"
        "<div class=\"control-group\">"
        "<label for=\"aec\">Auto Exposure Control:</label>"
        "<select id=\"aec\">"
        "<option value=\"1\">On (Automatic)</option>"
        "<option value=\"0\">Off (Manual)</option>"
        "</select>"
        "</div>"
        "<div class=\"control-group\">"
        "<label for=\"aec_value\">Manual Exposure Value:"
        "<span class=\"value-display\" id=\"aec_value_display\">300</span>"
        "</label>"
        "<input type=\"range\" id=\"aec_value\" min=\"0\" max=\"1200\" value=\"300\">"
        "</div>"
        "<div class=\"control-group\">"
        "<label for=\"ae_level\">Exposure Compensation:"
        "<span class=\"value-display\" id=\"ae_level_display\">0</span>"
        "</label>"
        "<input type=\"range\" id=\"ae_level\" min=\"-2\" max=\"2\" value=\"0\" step=\"1\">"
        "</div>"
        "<hr>"
        "<div class=\"control-group\">"
        "<label for=\"gain_ctrl\">Auto Gain Control:</label>"
        "<select id=\"gain_ctrl\">"
        "<option value=\"1\">On (Automatic)</option>"
        "<option value=\"0\">Off (Manual)</option>"
        "</select>"
        "</div>"
        "<div class=\"control-group\">"
        "<label for=\"agc_gain\">Manual Gain Value:"
        "<span class=\"value-display\" id=\"agc_gain_display\">0</span>"
        "</label>"
        "<input type=\"range\" id=\"agc_gain\" min=\"0\" max=\"30\" value=\"0\">"
        "</div>"
        "<p class=\"status-text\" id=\"status\"></p>"
        "<div class=\"button-group\">"
        "<button class=\"button\" onclick=\"applySettings()\">Apply Settings and Return Home</button>"
        "<a class=\"button secondary\" href=\"/\">Cancel</a>"
        "</div>"
        "</div>"
        "</div>"
        "<script>"
        "document.getElementById('aec_value').oninput = function() {"
        "  document.getElementById('aec_value_display').textContent = this.value;"
        "};"
        "document.getElementById('ae_level').oninput = function() {"
        "  document.getElementById('ae_level_display').textContent = this.value;"
        "};"
        "document.getElementById('agc_gain').oninput = function() {"
        "  document.getElementById('agc_gain_display').textContent = this.value;"
        "};"
        "function loadCurrentSettings() {"
        "  fetch('/status')"
        "  .then(function(response) { return response.json(); })"
        "  .then(function(data) {"
        "    document.getElementById('aec').value = data.aec_sensor ? '1' : '0';"
        "    document.getElementById('aec_value').value = data.aec_value || 300;"
        "    document.getElementById('aec_value_display').textContent = data.aec_value || 300;"
        "    document.getElementById('ae_level').value = data.ae_level || 0;"
        "    document.getElementById('ae_level_display').textContent = data.ae_level || 0;"
        "    document.getElementById('gain_ctrl').value = data.gain_ctrl ? '1' : '0';"
        "    document.getElementById('agc_gain').value = data.agc_gain || 0;"
        "    document.getElementById('agc_gain_display').textContent = data.agc_gain || 0;"
        "    document.getElementById('loading').style.display = 'none';"
        "    document.getElementById('settings').style.display = 'block';"
        "  })"
        "  .catch(function(err) {"
        "    console.error('Error loading settings:', err);"
        "    document.getElementById('loading').textContent = 'Error loading settings. Using defaults.';"
        "    document.getElementById('settings').style.display = 'block';"
        "  });"
        "}"
        "function applySettings() {"
        "  var status = document.getElementById('status');"
        "  status.textContent = 'Applying settings...';"
        "  var aec = document.getElementById('aec').value;"
        "  var aecValue = document.getElementById('aec_value').value;"
        "  var aeLevel = document.getElementById('ae_level').value;"
        "  var gainCtrl = document.getElementById('gain_ctrl').value;"
        "  var agcGain = document.getElementById('agc_gain').value;"
        "  console.log('Applying: AEC=' + aec + ', AECval=' + aecValue + ', AELevel=' + aeLevel + ', Gain=' + gainCtrl + ', AGCval=' + agcGain);"
        "  Promise.all(["
        "    fetch('/control?var=aec&val=' + aec),"
        "    fetch('/control?var=aec_value&val=' + aecValue),"
        "    fetch('/control?var=ae_level&val=' + aeLevel),"
        "    fetch('/control?var=gain_ctrl&val=' + gainCtrl),"
        "    fetch('/control?var=agc_gain&val=' + agcGain)"
        "  ]).then(function() {"
        "    status.textContent = 'Settings applied successfully! Returning home...';"
        "    setTimeout(function() {"
        "      window.location.href = '/';"
        "    }, 1500);"
        "  }).catch(function(err) {"
        "    console.error('Error applying settings:', err);"
        "    status.textContent = 'Error applying settings. Please try again.';"
        "  });"
        "}"
        "window.onload = function() {"
        "  loadCurrentSettings();"
        "};"
        "</script>"
        "</body>"
        "</html>";
    
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, html, strlen(html));
    return ESP_OK;
}

/**
 * @brief MJPEG stream handler - provides live video feed
 */
static esp_err_t stream_handler(httpd_req_t *req)
{
    camera_fb_t *fb = NULL;
    esp_err_t res = ESP_OK;
    char part_buf[128];
    
    ESP_LOGI(TAG, "Stream started");
    
    // Set response headers for MJPEG stream
    httpd_resp_set_type(req, "multipart/x-mixed-replace; boundary=frame");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "X-Framerate", "10");
    
    // Get quality parameter from URL query (default to 8 for medium quality)
    char query[64];
    int quality = 8;
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        char param[8];
        if (httpd_query_key_value(query, "quality", param, sizeof(param)) == ESP_OK) {
            quality = atoi(param);
            ESP_LOGI(TAG, "Stream quality parameter: %d", quality);
        }
    }
    
    // Temporarily switch to lower resolution for streaming
    sensor_t *s = esp_camera_sensor_get();
    if (s == NULL) {
        ESP_LOGE(TAG, "Failed to get camera sensor");
        return ESP_FAIL;
    }
    
    // Save original settings
    framesize_t original_framesize = s->status.framesize;
    int original_quality = s->status.quality;
    
    ESP_LOGI(TAG, "Original framesize: %d, quality: %d", original_framesize, original_quality);
    
    // Set streaming settings (VGA = 640x480 for smooth streaming)
    s->set_framesize(s, FRAMESIZE_VGA);
    s->set_quality(s, quality);
    
    ESP_LOGI(TAG, "Set stream to VGA, quality: %d", quality);
    
    // Stream frames continuously
    while (true) {
        fb = esp_camera_fb_get();
        if (!fb) {
            ESP_LOGE(TAG, "Camera capture failed during stream");
            res = ESP_FAIL;
            break;
        }
        
        // Send MJPEG frame boundary and headers
        size_t hlen = snprintf(part_buf, sizeof(part_buf),
                              "--frame\r\n"
                              "Content-Type: image/jpeg\r\n"
                              "Content-Length: %u\r\n\r\n",
                              fb->len);
        
        res = httpd_resp_send_chunk(req, part_buf, hlen);
        if (res != ESP_OK) {
            esp_camera_fb_return(fb);
            break;
        }
        
        // Send JPEG data
        res = httpd_resp_send_chunk(req, (const char *)fb->buf, fb->len);
        if (res != ESP_OK) {
            esp_camera_fb_return(fb);
            break;
        }
        
        // Send final boundary
        res = httpd_resp_send_chunk(req, "\r\n", 2);
        if (res != ESP_OK) {
            esp_camera_fb_return(fb);
            break;
        }
        
        esp_camera_fb_return(fb);
        fb = NULL;
        
        // Small delay between frames (100ms = ~10 FPS)
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    // Restore original settings
    s->set_framesize(s, original_framesize);
    s->set_quality(s, original_quality);
    
    ESP_LOGI(TAG, "Stream ended");
    return res;
}

/**
 * @brief Capture image handler - returns JPEG image
 */
static esp_err_t capture_handler(httpd_req_t *req)
{
    int64_t start_time = esp_timer_get_time();
    
    ESP_LOGI(TAG, "Image capture requested");
    
    // Capture image
    camera_fb_t *fb = camera_capture_image();
    if (!fb) {
        const char* error_msg = "Failed to capture image";
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_send(req, error_msg, strlen(error_msg));
        return ESP_FAIL;
    }
    
    int64_t capture_time = esp_timer_get_time();
    ESP_LOGI(TAG, "Image captured: %d bytes, %dx%d (capture: %lld ms)", 
             fb->len, fb->width, fb->height,
             (capture_time - start_time) / 1000);
    
    // Send image
    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.jpg");
    
    esp_err_t res = httpd_resp_send(req, (const char *)fb->buf, fb->len);
    
    int64_t send_time = esp_timer_get_time();
    ESP_LOGI(TAG, "Image sent (send: %lld ms, total: %lld ms)", 
             (send_time - capture_time) / 1000,
             (send_time - start_time) / 1000);
    
    // Return frame buffer
    esp_camera_fb_return(fb);
    
    return res;
}

/**
 * @brief Status handler - returns JSON status
 */
static esp_err_t status_handler(httpd_req_t *req)
{
    const char* json_response = 
        "{"
        "\"status\":\"ready\","
        "\"camera\":\"OV3660\","
        "\"resolution\":\"QXGA\","
        "\"width\":2048,"
        "\"height\":1536,"
        "\"format\":\"JPEG\","
        "\"psram\":true"
        "}";
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_response, strlen(json_response));
    return ESP_OK;
}

/**
 * @brief Control handler - adjust camera settings
 */
static esp_err_t control_handler(httpd_req_t *req)
{
    char buf[128];
    char var[32];
    char val[32];
    
    ESP_LOGI(TAG, "Control handler called");
    
    // Get query string
    if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get query string");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Query string: %s", buf);
    
    // Parse var parameter
    if (httpd_query_key_value(buf, "var", var, sizeof(var)) != ESP_OK ||
        httpd_query_key_value(buf, "val", val, sizeof(val)) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to parse parameters");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    
    int value = atoi(val);
    ESP_LOGI(TAG, "Control request: %s = %d", var, value);
    
    sensor_t *s = esp_camera_sensor_get();
    if (s == NULL) {
        ESP_LOGE(TAG, "Failed to get camera sensor");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    
    int res = 0;
    
    // Apply camera setting based on variable name
    if (strcmp(var, "aec") == 0) {
        // Auto Exposure Control
        res = s->set_exposure_ctrl(s, value);
        ESP_LOGI(TAG, ">>> Set AEC (Auto Exposure Control) to %d, result: %d", value, res);
    }
    else if (strcmp(var, "aec_value") == 0) {
        // Manual Exposure Value
        res = s->set_aec_value(s, value);
        ESP_LOGI(TAG, ">>> Set AEC_VALUE (Manual Exposure) to %d, result: %d", value, res);
    }
    else if (strcmp(var, "ae_level") == 0) {
        // AE Level (exposure compensation)
        res = s->set_ae_level(s, value);
        ESP_LOGI(TAG, ">>> Set AE_LEVEL (Exposure Compensation) to %d, result: %d", value, res);
    }
    else if (strcmp(var, "gain_ctrl") == 0) {
        // Auto Gain Control
        res = s->set_gain_ctrl(s, value);
        ESP_LOGI(TAG, ">>> Set GAIN_CTRL (Auto Gain Control) to %d, result: %d", value, res);
    }
    else if (strcmp(var, "agc_gain") == 0) {
        // Manual Gain Value
        res = s->set_agc_gain(s, value);
        ESP_LOGI(TAG, ">>> Set AGC_GAIN (Manual Gain) to %d, result: %d", value, res);
    }
    else {
        ESP_LOGW(TAG, "Unknown control variable: %s", var);
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }
    
    // Send response
    if (res == 0) {
        httpd_resp_set_type(req, "text/plain");
        httpd_resp_send(req, "OK", 2);
        return ESP_OK;
    } else {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
}

/**
 * @brief Favicon handler - returns 204 No Content
 */
static esp_err_t favicon_get_handler(httpd_req_t *req)
{
    httpd_resp_set_status(req, "204 No Content");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

/**
 * @brief URI handler structure for root page
 */
static const httpd_uri_t root_uri = {
    .uri       = "/",
    .method    = HTTP_GET,
    .handler   = root_get_handler,
    .user_ctx  = NULL
};

/**
 * @brief URI handler structure for capture endpoint
 */
static const httpd_uri_t capture_uri = {
    .uri       = "/capture",
    .method    = HTTP_GET,
    .handler   = capture_handler,
    .user_ctx  = NULL
};

/**
 * @brief URI handler structure for preview page
 */
static const httpd_uri_t preview_uri = {
    .uri       = "/preview",
    .method    = HTTP_GET,
    .handler   = preview_get_handler,
    .user_ctx  = NULL
};

/**
 * @brief URI handler structure for settings page
 */
static const httpd_uri_t settings_uri = {
    .uri       = "/settings",
    .method    = HTTP_GET,
    .handler   = settings_get_handler,
    .user_ctx  = NULL
};

/**
 * @brief URI handler structure for MJPEG stream
 */
static const httpd_uri_t stream_uri = {
    .uri       = "/stream",
    .method    = HTTP_GET,
    .handler   = stream_handler,
    .user_ctx  = NULL
};

/**
 * @brief URI handler structure for status endpoint
 */
static const httpd_uri_t status_uri = {
    .uri       = "/status",
    .method    = HTTP_GET,
    .handler   = status_handler,
    .user_ctx  = NULL
};

/**
 * @brief URI handler structure for control endpoint
 */
static const httpd_uri_t control_uri = {
    .uri       = "/control",
    .method    = HTTP_GET,
    .handler   = control_handler,
    .user_ctx  = NULL
};

/**
 * @brief URI handler structure for favicon
 */
static const httpd_uri_t favicon_uri = {
    .uri       = "/favicon.ico",
    .method    = HTTP_GET,
    .handler   = favicon_get_handler,
    .user_ctx  = NULL
};

httpd_handle_t start_webserver(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 16;
    config.stack_size = 8192;
    
    ESP_LOGI(TAG, "Starting HTTP server on port %d", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK) {
        ESP_LOGI(TAG, "Registering URI handlers");
        httpd_register_uri_handler(server, &root_uri);
        httpd_register_uri_handler(server, &preview_uri);
        httpd_register_uri_handler(server, &settings_uri);
        httpd_register_uri_handler(server, &stream_uri);
        httpd_register_uri_handler(server, &capture_uri);
        httpd_register_uri_handler(server, &status_uri);
        httpd_register_uri_handler(server, &control_uri);
        httpd_register_uri_handler(server, &favicon_uri);
        ESP_LOGI(TAG, "HTTP server started successfully");
        return server;
    }

    ESP_LOGE(TAG, "Failed to start HTTP server");
    return NULL;
}

void stop_webserver(httpd_handle_t server)
{
    if (server) {
        httpd_stop(server);
        ESP_LOGI(TAG, "HTTP server stopped");
    }
}
