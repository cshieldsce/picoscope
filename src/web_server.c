#include "web_server.h"

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"

#include "lwip/sockets.h"
#include "lwip/err.h"
#include "lwip/netif.h"
#include "lwip/ip4_addr.h"

#define WEBSERVER_PORT ( 80 )

/*
 * Task (Core 0): Wi-Fi Web Server
 */
void vWebServerTask(void *pvParameters) {
    cyw43_arch_enable_sta_mode();

    WifiCredentials_t *creds = (WifiCredentials_t *)pvParameters;

    printf("Connecting to WiFi SSID: %s\n", creds->pcWifiName);
    if (cyw43_arch_wifi_connect_timeout_ms(creds->pcWifiName, creds->pcWifiPass, CYW43_AUTH_WPA2_AES_PSK, 30000)) {
        printf("WiFi connect failed\n");
        vTaskDelete(NULL);
    }

    printf("IP Address: %s\n", ip4addr_ntoa(netif_ip4_addr(netif_list)));    
    vTaskDelay(pdMS_TO_TICKS(1000));
        
    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    
    printf("socket() returned: %d\n", sock);
    if (sock < 0) {
        printf("socket() failed\n");
        vTaskDelete(NULL);
    }
    
    int opt = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(WEBSERVER_PORT),
        .sin_addr = { .s_addr = INADDR_ANY }
    };

    if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        printf("bind() failed\n");
        close(sock);
        vTaskDelete(NULL);
    }

    if (listen(sock, 1) < 0) {
        printf("listen() failed\n");
        close(sock);
        vTaskDelete(NULL);
    }

    printf("HTTP server listening on port %d\n", WEBSERVER_PORT);

    for (;;) {
        struct sockaddr_in client;
        socklen_t clientlen = sizeof(client);
        int clientfd = accept(sock, (struct sockaddr*)&client, &clientlen);
        if (clientfd >= 0) {
            const char *resp = "HTTP/1.0 200 OK\r\nContent-Length:5\r\n\r\nHallo";
            send(clientfd, resp, strlen(resp), 0);
            close(clientfd);
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
