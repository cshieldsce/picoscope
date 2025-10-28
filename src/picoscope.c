#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"

#include "FreeRTOS.h"
#include "task.h"

#include "lwip/sockets.h"
#include "lwip/err.h"
#include "lwip/netif.h"
#include "lwip/ip4_addr.h"
#include <string.h>

#define HTTP_PORT 80

static TaskHandle_t http_task_handle = NULL;
static TaskHandle_t blink_task_handle = NULL;

const char *ssid = "";
const char *pass = "";

// Blink LED task
static void blink_task(void *pv) {
    for (;;) {
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
        vTaskDelay(pdMS_TO_TICKS(250));
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
        vTaskDelay(pdMS_TO_TICKS(250));
    }
}

// HTTP server task
static void http_task(void *pv) {
    cyw43_arch_enable_sta_mode();

    printf("Connecting to WiFi SSID: %s\n", ssid);
    if (cyw43_arch_wifi_connect_timeout_ms(ssid, pass, CYW43_AUTH_WPA2_AES_PSK, 30000)) {
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
        .sin_port = htons(HTTP_PORT),
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

    printf("HTTP server listening on port %d\n", HTTP_PORT);

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

// Init task - initializes CYW43 then creates other tasks and deletes itself
static void init_task(void *pv) {
    printf("Init task started\n");
    
    if (cyw43_arch_init()) {
        printf("CYW43 init failed\n");
        for(;;);
    }
    printf("CYW43 init OK\n");
    
    xTaskCreate(blink_task, "blink", 1024, NULL, 2, &blink_task_handle);
    xTaskCreate(http_task, "http", 4096, NULL, 1, &http_task_handle);
        
    vTaskDelete(NULL);
}

int main() {
    stdio_init_all();
    sleep_ms(2000);

    printf("Starting FreeRTOS + BSD socket HTTP example\n");

    xTaskCreate(init_task, "init", 2048, NULL, 3, NULL);

    vTaskStartScheduler();

    for (;;) tight_loop_contents();
}