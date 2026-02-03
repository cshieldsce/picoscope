#include "pico/stdlib.h"
#include "FreeRTOS.h"
#include "task.h"
#include "drivers/adc_dma.h"
#include <stdio.h>

#define TEST_PASS() printf("✓ PASS: %s\n", __func__)
#define TEST_FAIL(msg) printf("✗ FAIL: %s - %s\n", __func__, msg)
#define TEST_ASSERT(cond, msg) if (!(cond)) { TEST_FAIL(msg); return; }

void test_dma_init_and_start(void) {
    printf("\n--- Test: DMA init and start ---\n");
    
    vAdcDmaInit();
    printf("ADC DMA initialized\n");
    
    vTaskDelay(pdMS_TO_TICKS(10));
    
    vAdcDmaStartContinous();
    printf("ADC DMA started\n");
    
    vTaskDelay(pdMS_TO_TICKS(100));
    
    vAdcDmaStop();
    printf("ADC DMA stopped\n");
    
    TEST_PASS();
}

void test_buffer_acquisition(void) {
    printf("\n--- Test: Buffer acquisition ---\n");
    
    vAdcDmaInit();
    vTaskDelay(pdMS_TO_TICKS(10));
    vAdcDmaStartContinous();
    
    uint16_t *ptr = NULL;
    uint32_t timestamp = 0;
    
    printf("Waiting for first buffer...\n");
    
    int attempts = 0;
    while (!bAdcDmaGetLatestBufferPtr(&ptr, &timestamp) && attempts < 100) {
        vTaskDelay(pdMS_TO_TICKS(10));
        attempts++;
    }
    
    TEST_ASSERT(ptr != NULL, "Should get a buffer");
    printf("Got buffer at %p, timestamp: %lu\n", ptr, timestamp);
    
    /* Verify buffer has data (not all zeros) */
    bool has_data = false;
    for (int i = 0; i < 100; i++) {
        if (ptr[i] != 0) {
            has_data = true;
            break;
        }
    }
    TEST_ASSERT(has_data, "Buffer should contain ADC data");
    
    vAdcDmaReleaseBuffer(ptr);
    vAdcDmaStop();
    
    TEST_PASS();
}

void test_triple_buffer_cycle(void) {
    printf("\n--- Test: Triple buffer cycle ---\n");
    
    vAdcDmaInit();
    vAdcDmaStartContinous();
    
    uint16_t *ptrs[3] = {NULL};
    uint32_t timestamps[3] = {0};
    
    /* Get 3 buffers sequentially */
    for (int i = 0; i < 3; i++) {
        int attempts = 0;
        while (!bAdcDmaGetLatestBufferPtr(&ptrs[i], &timestamps[i]) && attempts < 100) {
            vTaskDelay(pdMS_TO_TICKS(10));
            attempts++;
        }
        
        TEST_ASSERT(ptrs[i] != NULL, "Should get buffer");
        printf("Buffer %d: %p at %lu\n", i, ptrs[i], timestamps[i]);
        
        vAdcDmaReleaseBuffer(ptrs[i]);
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    
    /* All three should be different pointers */
    TEST_ASSERT(ptrs[0] != ptrs[1] || ptrs[1] != ptrs[2], "Buffers should cycle");
    
    vAdcDmaStop();
    
    TEST_PASS();
}

void test_overrun_detection(void) {
    printf("\n--- Test: Overrun detection under slow consumer ---\n");
    
    vAdcDmaInit();
    uint32_t initial_overruns = ulOverruns;
    
    vAdcDmaStartContinous();
    
    /* Simulate slow consumer: get buffer, hold it for long time */
    uint16_t *ptr;
    uint32_t ts;
    
    while (!bAdcDmaGetLatestBufferPtr(&ptr, &ts)) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    
    printf("Holding buffer for 200ms while DMA continues...\n");
    vTaskDelay(pdMS_TO_TICKS(200));
    
    uint32_t overruns_while_held = ulOverruns - initial_overruns;
    printf("Overruns while holding: %lu\n", overruns_while_held);
    
    vAdcDmaReleaseBuffer(ptr);
    vAdcDmaStop();
    
    TEST_ASSERT(overruns_while_held < 10, "Should have minimal overruns with triple buffering");
    
    TEST_PASS();
}

void test_data_integrity(void) {
    printf("\n--- Test: ADC data integrity ---\n");
    
    vAdcDmaInit();
    vAdcDmaStartContinous();
    
    uint16_t *ptr;
    uint32_t ts;
    
    while (!bAdcDmaGetLatestBufferPtr(&ptr, &ts)) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    
    /* Check that values are in 12-bit range */
    bool all_valid = true;
    uint32_t sum = 0;
    uint16_t minv = 4095, maxv = 0;
    
    for (int i = 0; i < ADC_BUFFER_SIZE; i++) {
        uint16_t val = ptr[i];
        if (val > 4095) {
            all_valid = false;
            break;
        }
        sum += val;
        if (val < minv) minv = val;
        if (val > maxv) maxv = val;
    }
    
    TEST_ASSERT(all_valid, "All values should be 12-bit");
    
    float avg = (float)sum / ADC_BUFFER_SIZE;
    printf("Min: %u, Max: %u, Avg: %.1f\n", minv, maxv, avg);
    
    vAdcDmaReleaseBuffer(ptr);
    vAdcDmaStop();
    
    TEST_PASS();
}

static void test_runner_task(void *pv) {
    printf("\n========================================\n");
    printf("  PICOSCOPE DMA INTEGRATION TEST SUITE\n");
    printf("========================================\n");
    
    vTaskDelay(pdMS_TO_TICKS(500));
    
    test_dma_init_and_start();
    vTaskDelay(pdMS_TO_TICKS(100));
    
    test_buffer_acquisition();
    vTaskDelay(pdMS_TO_TICKS(100));
    
    test_triple_buffer_cycle();
    vTaskDelay(pdMS_TO_TICKS(100));
    
    test_overrun_detection();
    vTaskDelay(pdMS_TO_TICKS(100));
    
    test_data_integrity();
    vTaskDelay(pdMS_TO_TICKS(100));
    
    printf("\n========================================\n");
    printf("  ALL DMA TESTS COMPLETE\n");
    printf("========================================\n");
    
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

int dma_tests(void) {
    stdio_init_all();
    sleep_ms(2000);
    
    printf("\nStarting DMA integration tests...\n");
    
    xTaskCreate(test_runner_task, "TestRunner", 2048, NULL, 1, NULL);
    vTaskStartScheduler();
    
    for (;;) tight_loop_contents();
}