#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"

#include "FreeRTOS.h"
#include "task.h"

// The simple task to blink the LED
void vBlinkTask(void *pvParameters) {
    printf("BlinkTask: Started!\n");
    
    while (true) {
        printf("BlinkTask: LED ON\n");
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
        vTaskDelay(pdMS_TO_TICKS(250));
        
        printf("BlinkTask: LED OFF\n");
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
        vTaskDelay(pdMS_TO_TICKS(750));
    }
}

int main() {
    stdio_init_all();
    
    sleep_ms(5000); 
    printf("--- Pico 2 W FreeRTOS Test ---\n");

    // This is REQUIRED to control the LED on a Pico 2 W
    printf("Initializing CYW43 chip...\n");
    if (cyw43_arch_init()) {
        printf("FAILED to initialize CYW43 chip.\n");
        return -1;
    }
    printf("CYW43 initialized successfully.\n");

    // Create the task
    // *** INCREASED STACK SIZE FROM 128 to 256 ***
    // 128 words (512 bytes) is often too small and causes a stack overflow.
    printf("Creating blink task...\n");
    BaseType_t result = xTaskCreate(
        vBlinkTask,           // The function that implements the task.
        "BlinkTask",          // A name for the task (for debugging).
        256,                  // The stack size (in words) for the task.
        NULL,                 // Parameter passed into the task (not used).
        1,                    // The task priority.
        NULL                  // Task handle (not used).
    );

    if (result != pdPASS) {
        // This will tell us if FreeRTOS ran out of memory
        printf("FAILED to create task! (Out of heap?)\n");
        return -1;
    }
    printf("Blink task created.\n");

    // Start the scheduler
    printf("Starting FreeRTOS scheduler...\n");
    vTaskStartScheduler();

    // This part is never reached
    while (1);
}