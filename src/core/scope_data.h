#ifndef SCOPE_DATA_H
#define SCOPE_DATA_H

#include <stdint.h>
#include <stdbool.h>
#include "FreeRTOS.h"
#include "task.h"
#include "drivers/adc_dma.h"

/* 
 * Zero-copy scope data model
 *
 * Producer (Acquisition task):
 *  - Obtains a DMA buffer pointer via bAdcDmaGetLatestBufferPtr()
 *  - Publishes it to the scope layer via vScopeDataPublishBuffer()
 *
 * Consumer (Web server task):
 *  - Calls bGetLatestScopeData() to obtain a stable snapshot pointer
 *  - The scope layer keeps that buffer owned ("in use") until the next consume,
 *    then releases the previous buffer back to ADC via vAdcDmaReleaseBuffer()
 *
 * This prevents DMA from overwriting the buffer being streamed.
 */

typedef struct {
    uint16_t *pusSamples;        /* Pointer to DMA buffer memory */
    uint32_t ulTimestamp;        /* Capture completion time (ms since boot) */
    float    avg_voltage;        /* Lazily computed statistics */
    float    min_voltage;
    float    max_voltage;
    volatile bool bStatsValid;   /* False => stats to be computed */
} ScopeBuffer_t;

/* Initialize scope data system */
void vScopeDataInit(void);

/* Called by acquisition task when a DMA buffer completes */
void vScopeDataPublishBuffer(uint16_t *buffer, uint32_t timestamp);

/* Set web server task handle for notifications (xTaskNotifyGive) */
void vScopeDataSetWebServerHandle(TaskHandle_t handle);

/* Get latest scope data snapshot, optionally computing stats.
 * Returns true and fills pData with a stable pointer and stats.
 */
bool bGetLatestScopeData(ScopeBuffer_t *pData, bool bCalculateStats);

/* Release the currently held buffer (allows promotion of xReady) */
void vScopeDataReleaseBuffer(void);

#endif /* SCOPE_DATA_H */