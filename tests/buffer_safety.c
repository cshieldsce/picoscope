#include "pico/stdlib.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "core/scope_data.h"
#include "drivers/adc_dma.h"
#include <stdio.h>
#include <string.h>

#define TEST_PASS() printf("✓ PASS: %s\n", __func__)
#define TEST_FAIL(msg) printf("✗ FAIL: %s - %s\n", __func__, msg)
#define TEST_ASSERT(cond, msg) if (!(cond)) { TEST_FAIL(msg); return; } 

static volatile uint32_t g_test_notifications = 0;
static volatile bool g_test_running = false;
static SemaphoreHandle_t g_test_done;

/* Mock web server task that counts notifications */
static void test_notification_counter_task(void *pv) {
    while (g_test_running) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100));
        g_test_notifications++;
    }
    vTaskDelete(NULL);
}

void test_scope_data_notifies_web_server(void) {
    printf("\n--- Test: Scope data notifies web server ---\n");
    
    vScopeDataInit();
    g_test_notifications = 0;
    g_test_running = true;
    
    TaskHandle_t web_handle;
    xTaskCreate(test_notification_counter_task, "TestWeb", 256, NULL, 1, &web_handle);
    vScopeDataSetWebServerHandle(web_handle);
    
    /* Simulate 5 buffer publishes */
    uint16_t test_buf[ADC_BUFFER_SIZE];
    memset(test_buf, 0x55, sizeof(test_buf));
    
    for (int i = 0; i < 5; i++) {
        vScopeDataPublishBuffer(test_buf, i * 1000);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    
    g_test_running = false;
    vTaskDelay(pdMS_TO_TICKS(50));
    
    printf("Notifications received: %lu (expected: 5)\n", g_test_notifications);
    TEST_ASSERT(g_test_notifications == 5, "Should receive 5 notifications");
    
    TEST_PASS();
}

void test_rapid_publish_drops_old_frames(void) {
    printf("\n--- Test: Rapid publish drops old frames ---\n");
    
    vScopeDataInit();
    
    uint16_t buf1[ADC_BUFFER_SIZE];
    uint16_t buf2[ADC_BUFFER_SIZE];
    uint16_t buf3[ADC_BUFFER_SIZE];
    
    /* Fill with identifiable patterns */
    memset(buf1, 0x11, sizeof(buf1));
    memset(buf2, 0x22, sizeof(buf2));
    memset(buf3, 0x33, sizeof(buf3));
    
    /* Publish three rapidly */
    vScopeDataPublishBuffer(buf1, 100);
    vScopeDataPublishBuffer(buf2, 200);
    vScopeDataPublishBuffer(buf3, 300);
    
    /* Should get newest (buf3) */
    ScopeBuffer_t result;
    bool got_data = bGetLatestScopeData(&result, false);
    
    TEST_ASSERT(got_data, "Should have data");
    TEST_ASSERT(result.pusSamples == buf3, "Should get newest buffer");
    TEST_ASSERT(result.ulTimestamp == 300, "Timestamp should be 300");
    
    printf("Got buffer at timestamp: %lu (expected: 300)\n", result.ulTimestamp);
    
    TEST_PASS();
}

void test_statistics_calculation(void) {
    printf("\n--- Test: Statistics calculation ---\n");
    
    vScopeDataInit();
    
    uint16_t test_buf[ADC_BUFFER_SIZE];
    
    /* Known pattern: all values = 2047 (mid-scale) */
    for (int i = 0; i < ADC_BUFFER_SIZE; i++) {
        test_buf[i] = 2047;
    }
    test_buf[0] = 0;      // min
    test_buf[1] = 4095;   // max
    
    vScopeDataPublishBuffer(test_buf, 500);
    
    ScopeBuffer_t result;
    bool got_data = bGetLatestScopeData(&result, true);
    
    TEST_ASSERT(got_data, "Should have data");
    TEST_ASSERT(result.bStatsValid, "Stats should be valid");
    
    printf("Min: %.3f V (expected: ~0.0)\n", result.min_voltage);
    printf("Max: %.3f V (expected: ~3.3)\n", result.max_voltage);
    printf("Avg: %.3f V (expected: ~1.65)\n", result.avg_voltage);
    
    TEST_ASSERT(result.min_voltage < 0.1, "Min should be near 0V");
    TEST_ASSERT(result.max_voltage > 3.2, "Max should be near 3.3V");
    TEST_ASSERT(result.avg_voltage > 1.5 && result.avg_voltage < 1.8, "Avg should be ~1.65V");
    
    TEST_PASS();
}

void test_buffer_ownership_transfer(void) {
    printf("\n--- Test: Buffer ownership transfer ---\n");
    
    vScopeDataInit();
    
    uint16_t buf1[ADC_BUFFER_SIZE];
    uint16_t buf2[ADC_BUFFER_SIZE];
    memset(buf1, 0xAA, sizeof(buf1));
    memset(buf2, 0xBB, sizeof(buf2));
    
    /* Publish buf1 */
    vScopeDataPublishBuffer(buf1, 100);
    
    ScopeBuffer_t held;
    bool got = bGetLatestScopeData(&held, false);
    TEST_ASSERT(got && held.pusSamples == buf1, "Should get buf1");
    
    printf("Consumer holds buf1 at %p\n", held.pusSamples);
    
    /* Publish buf2 while consumer holds buf1 */
    vScopeDataPublishBuffer(buf2, 200);
    
    /* Get again - should still be buf1 (xInUse not released yet) */
    ScopeBuffer_t still_held;
    got = bGetLatestScopeData(&still_held, false);
    TEST_ASSERT(got && still_held.pusSamples == buf1, "Should still get buf1");
    
    printf("Still holding buf1 at %p (buf2 is ready)\n", still_held.pusSamples);
    
    /* FIXED: Explicitly release buf1 */
    vScopeDataReleaseBuffer();
    
    /* Get once more - now should promote buf2 */
    ScopeBuffer_t promoted;
    got = bGetLatestScopeData(&promoted, false);
    TEST_ASSERT(got && promoted.pusSamples == buf2, "Should now get buf2");
    TEST_ASSERT(promoted.ulTimestamp == 200, "Timestamp should be 200");
    
    printf("Promoted to buf2 at %p\n", promoted.pusSamples);
    
    /* Clean up */
    vScopeDataReleaseBuffer();
    
    TEST_PASS();
}

/* Stress test: concurrent producer/consumer */
static volatile uint32_t g_acquire_count = 0;
static volatile uint32_t g_consume_count = 0;
static volatile uint32_t g_corruption_count = 0;

/* FIXED: Use a proper test buffer pool that mimics DMA behavior */
#define TEST_BUFFER_POOL_SIZE 4
static uint16_t test_buffer_pool[TEST_BUFFER_POOL_SIZE][ADC_BUFFER_SIZE];
static volatile bool test_buffer_in_use[TEST_BUFFER_POOL_SIZE] = {false};

/* Test-only: Mock version of vAdcDmaReleaseBuffer for stress test */
static void test_release_buffer(uint16_t* ptr) {
    for (int i = 0; i < TEST_BUFFER_POOL_SIZE; i++) {
        if (test_buffer_pool[i] == ptr) {
            test_buffer_in_use[i] = false;
            return;
        }
    }
}

/* Test-only: Allocate a buffer from test pool */
static uint16_t* test_allocate_buffer(void) {
    for (int i = 0; i < TEST_BUFFER_POOL_SIZE; i++) {
        if (!test_buffer_in_use[i]) {
            test_buffer_in_use[i] = true;
            return test_buffer_pool[i];
        }
    }
    return NULL;  // All buffers in use (simulates overrun)
}

static void stress_acquire_task(void *pv) {
    while (g_test_running) {
        /* Get a free buffer from test pool */
        uint16_t* buf = test_allocate_buffer();
        
        if (buf != NULL) {
            uint16_t pattern = (uint16_t)(g_acquire_count & 0xFFFF);
            
            /* Fill buffer with test pattern */
            for (int i = 0; i < ADC_BUFFER_SIZE; i++) {
                buf[i] = pattern;
            }
            
            vScopeDataPublishBuffer(buf, g_acquire_count);
            g_acquire_count++;
        } else {
            /* Simulate overrun - no free buffers */
            vTaskDelay(pdMS_TO_TICKS(1));
        }
        
        vTaskDelay(pdMS_TO_TICKS(1 + (g_acquire_count % 3)));
    }
    
    xSemaphoreGive(g_test_done);
    vTaskDelete(NULL);
}

/* FIXED: Use test release function instead of DMA release */
static void stress_consume_task(void *pv) {
    while (g_test_running) {
        ScopeBuffer_t data;
        if (bGetLatestScopeData(&data, false)) {
            if (data.pusSamples != NULL) {
                uint16_t expected = (uint16_t)(data.ulTimestamp & 0xFFFF);
                
                /* Check for corruption */
                for (int i = 0; i < ADC_BUFFER_SIZE; i++) {
                    if (data.pusSamples[i] != expected) {
                        g_corruption_count++;
                        break;
                    }
                }
                g_consume_count++;
                
                /* Release back to test pool (not DMA system) */
                test_release_buffer(data.pusSamples);
                
                /* Clear xInUse in scope_data to allow promotion */
                vScopeDataReleaseBuffer();
            }
        }
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    
    xSemaphoreGive(g_test_done);
    vTaskDelete(NULL);
}

void test_concurrent_stress(void) {
    printf("\n--- Test: Concurrent producer/consumer stress ---\n");
    
    vScopeDataInit();
    
    /* Initialize test buffer pool */
    for (int i = 0; i < TEST_BUFFER_POOL_SIZE; i++) {
        test_buffer_in_use[i] = false;
    }
    
    g_acquire_count = 0;
    g_consume_count = 0;
    g_corruption_count = 0;
    g_test_running = true;
    
    g_test_done = xSemaphoreCreateCounting(2, 0);
    
    TaskHandle_t acq, cons;
    xTaskCreate(stress_acquire_task, "StressAcq", 512, NULL, 2, &acq);
    xTaskCreate(stress_consume_task, "StressCons", 512, NULL, 2, &cons);
    
    printf("Running stress test for 3 seconds...\n");
    vTaskDelay(pdMS_TO_TICKS(3000));
    
    g_test_running = false;
    
    /* Wait for both to finish */
    xSemaphoreTake(g_test_done, portMAX_DELAY);
    xSemaphoreTake(g_test_done, portMAX_DELAY);
    
    printf("Acquired: %lu buffers\n", g_acquire_count);
    printf("Consumed: %lu buffers\n", g_consume_count);
    printf("Corruptions: %lu\n", g_corruption_count);
    
    TEST_ASSERT(g_corruption_count == 0, "No corruptions should occur");
    TEST_ASSERT(g_acquire_count > 100, "Should acquire many buffers");
    TEST_ASSERT(g_consume_count > 50, "Should consume many buffers");
    
    vSemaphoreDelete(g_test_done);
    
    TEST_PASS();
}

/* Main test runner task */
static void test_runner_task(void *pv) {
    printf("\n========================================\n");
    printf("  PICOSCOPE BUFFER SAFETY TEST SUITE\n");
    printf("========================================\n");
    
    vTaskDelay(pdMS_TO_TICKS(500));
    
    test_scope_data_notifies_web_server();
    vTaskDelay(pdMS_TO_TICKS(100));
    
    test_rapid_publish_drops_old_frames();
    vTaskDelay(pdMS_TO_TICKS(100));
    
    test_statistics_calculation();
    vTaskDelay(pdMS_TO_TICKS(100));
    
    test_buffer_ownership_transfer();
    vTaskDelay(pdMS_TO_TICKS(100));
    
    test_concurrent_stress();
    vTaskDelay(pdMS_TO_TICKS(100));
    
    printf("\n========================================\n");
    printf("  ALL TESTS COMPLETE\n");
    printf("========================================\n");
    
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

int main(void) {
    stdio_init_all();
    sleep_ms(2000);
    
    printf("\nStarting buffer safety tests...\n");
    
    xTaskCreate(test_runner_task, "TestRunner", 2048, NULL, 1, NULL);
    vTaskStartScheduler();
    
    for (;;) tight_loop_contents();
}