#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"

#include "FreeRTOS.h"
#include "task.h"

#include "web_server.h"

static TaskHandle_t xWebServerHandle = NULL;
static TaskHandle_t xBlinkHandle = NULL;

/*
 * Task (Core 0): Blink Task
 * Simple test to blink the onboard LED.
*/ 
static void vBlinkTask(void *pv) {
    for (;;) {
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
        vTaskDelay(pdMS_TO_TICKS(250));
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
        vTaskDelay(pdMS_TO_TICKS(250));
    }
}

/*
 * Task (Core 0): Init Task
 * Initializes CYW43 then creates other tasks and deletes itself.
*/ 
static void vInitTask(void *pv) {    
    if (cyw43_arch_init()) {
        printf("CYW43 init failed\n");
        for(;;);
    }

    /* Pass Wifi credentials to vWebServerTask */
    static WifiCredentials_t xWifiCredentials = {
        .pcWifiName = "picotest",
        .pcWifiPass = "testingtesting"
    };
    
    xTaskCreate(vBlinkTask, "vBlinkTask", 1024, NULL, 2, &xWebServerHandle);
    xTaskCreate(vWebServerTask, "vWebServerTask", 4096, &xWifiCredentials, 1, &xBlinkHandle);
        
    vTaskDelete(NULL);
}

int main() {
    /* Initialize USB serial then wait */
    stdio_init_all();
    sleep_ms(2000);

    /* Create our initialization task */
    xTaskCreate(vInitTask, "vInitTask", 2048, NULL, 3, NULL);

    /* Start the FreeRTOS Scheduler */
    vTaskStartScheduler();

    for (;;) tight_loop_contents();
}