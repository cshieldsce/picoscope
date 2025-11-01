#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include "FreeRTOS.h"
#include "task.h"

/* A struct to hold the parameters for the web server task. */
typedef struct {
    const char *pcWifiName;
    const char *pcWifiPass;
} WifiCredentials_t;

/*
 * Task (Core 0): Wi-Fi Web Server
 * Connects to Wi-Fi and serves a simple HTTP page.
 */
void vWebServerTask(void *pvParameters);

#endif
