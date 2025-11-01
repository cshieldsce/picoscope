#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"

#include "FreeRTOS.h"
#include "task.h"

#include "web_server.h"
#include "adc_dma.h"

static TaskHandle_t xWebServerHandle = NULL;
static TaskHandle_t xBlinkHandle = NULL;
static TaskHandle_t xAcquisitionTask = NULL;

/*
 * Task (Low Priority): Blink Task
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
 * Task 3 (High Priority): ADC Data Acquisition
 * Continuously acquires data from ADC using DMA and processes it.
 * Prints min, max, and average of each buffer.
 */
void vAcquisitionTask(void *pv) {
    printf("Acquisition Task: Started\n");
    
    uint16_t sample_buffer[ADC_BUFFER_SIZE];
    uint32_t capture_timestamp;
    
    vAdcDmaInit();
    vTaskDelay(pdMS_TO_TICKS(10));
    
    vAdcDmaStartContinous();
    
    while(true) {
        if (bAdcDmaGetLatestBuffer(sample_buffer, &capture_timestamp)) {
            uint32_t sum = 0;
            
            /* Calculate time of first sample
               Buffer capture completed at capture_timestamp
               Last sample was captured 2μs before that
               First sample was captured (BUFFER_SIZE * 2μs) before that */
            uint32_t start_time = capture_timestamp - ((ADC_BUFFER_SIZE * 2) / 1000); /* in ms */
            
            for (int i = 0; i < ADC_BUFFER_SIZE; i++) {
                sum += sample_buffer[i];

                /* Each sample's precise timestamp in milliseconds */
                float sample_time = start_time + ((i * 2.0f) / 1000.0f);
                
                /* Now we have accurate timing for each sample point 
                   We could use this for plotting or analysis */

                /* float voltage = (sample_buffer[i] * 3.3f) / 4095.0f;
                   printf("Sample[%d]: time=%.3fms voltage=%.3fV\n", i, sample_time, voltage); */
            }

            /* For now get an average of all the data and display it */
            float avg = (float)sum / ADC_BUFFER_SIZE;
            
            printf("Capture %lu-%lums: Voltage=%.2fV\n", 
                   start_time,
                   capture_timestamp,
                   (avg * 3.3f) / 4095.0f);
        }
        
        vTaskDelay(pdMS_TO_TICKS(1)); /* Small delay to prevent task starvation (will upgrade later) */
    }
}

/* 
 * Task (High Priority): Init Task
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
    
    xTaskCreate(vBlinkTask, "vBlinkTask", configMINIMAL_STACK_SIZE, NULL, 1, &xBlinkHandle);
    xTaskCreate(vWebServerTask, "vWebServerTask", 8192, &xWifiCredentials, 2, &xWebServerHandle);
    xTaskCreate(vAcquisitionTask, "vAcquisitionTask", 4096, NULL, 3, &xAcquisitionTask);
    
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