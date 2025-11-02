#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"

#include "FreeRTOS.h"
#include "task.h"

#include "net/web_server.h"
#include "drivers/adc_dma.h"
#include "core/scope_data.h"

static TaskHandle_t xWebServerHandle = NULL;
static TaskHandle_t xBlinkHandle = NULL;
static TaskHandle_t xAcquisitionHandle = NULL;

/*
 * Task: Blink LED
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
 * Task: ADC Data Acquisition
 * Captures data and sends to stream buffer
 */
static void vAcquisitionTask(void *pv) {
    uint32_t pulCaptureTimestamp;

    /* Initialize ADC DMA, let it rest, start first transfer */
    vAdcDmaInit();
    vTaskDelay(pdMS_TO_TICKS(10));
    vAdcDmaStartContinous();
    
    for (;;) {
        uint16_t *dma_buffer = NULL;
        
        /* Get pointer to latest DMA buffer */
        if (bAdcDmaGetLatestBufferPtr(&dma_buffer, &pulCaptureTimestamp)) {
            /* Pass DMA buffer pointer directly (zero-copy) */
            vScopeDataPublishBuffer(dma_buffer, pulCaptureTimestamp);
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

/* 
 * Task: Initialization
 */ 
static void vInitTask(void *pv) {
    printf("[Core%u] Init task started\n", get_core_num());
    
    /* Initialize WiFi */
    if (cyw43_arch_init()) {
        printf("CYW43 init failed\n");
        for(;;);
    }

    /* Initialize scope data system */
    vScopeDataInit();

    /* Create tasks */
    xTaskCreate(vBlinkTask, "Blink", configMINIMAL_STACK_SIZE, NULL, 1, &xBlinkHandle);
    xTaskCreate(vAcquisitionTask, "Acquisition", 4096, NULL, 3, &xAcquisitionHandle);
    
    /* Create web server */
    static WifiCredentials_t xWifiCredentials = {
        .pcWifiName = "picotest",
        .pcWifiPass = "testingtesting"
    };
    xTaskCreate(vWebServerTask, "WebServer", 8192, &xWifiCredentials, 2, &xWebServerHandle);
    
    /* Register web server handle for notifications */
    vScopeDataSetWebServerHandle(xWebServerHandle);
    
    printf("[Core%u] All tasks created\n", get_core_num());
    vTaskDelete(NULL);
}

int main() {
    stdio_init_all();
    sleep_ms(2000);
    
    printf("\n=== Picoscope ===\n");

    xTaskCreate(vInitTask, "Init", 2048, NULL, 3, NULL);
    vTaskStartScheduler();

    for (;;) tight_loop_contents();
}