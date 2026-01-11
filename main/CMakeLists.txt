idf_component_register(SRCS "main.c"
                            "camera/camera.c"
                            "wifi/wifi.c"
                            "web_server/web_server.c"
                    INCLUDE_DIRS "."
                    REQUIRES mdns esp_http_server esp_wifi nvs_flash esp_timer esp_psram)
