/**
 * @file web_server.h
 * @brief HTTP web server module
 */

#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include "esp_http_server.h"

/**
 * @brief Start the HTTP web server
 * @return httpd_handle_t Handle to the started server, NULL on failure
 */
httpd_handle_t start_webserver(void);

/**
 * @brief Stop the HTTP web server
 * @param server Handle to the server to stop
 */
void stop_webserver(httpd_handle_t server);

#endif // WEB_SERVER_H
