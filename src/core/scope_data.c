#include "scope_data.h"
#include "pico/stdlib.h"
#include <string.h>
#include <stdio.h>

/* Internal state: one ready slot and one in-use slot */
static ScopeBuffer_t xReady = { 0 };
static ScopeBuffer_t xInUse = { 0 };

/* Web server task handle for notifications */
static TaskHandle_t xWebServerHandle = NULL;

void vScopeDataInit(void) {
    /* Initialize internal state (set everything to zero) */
    memset(&xReady, 0, sizeof(xReady));
    memset(&xInUse, 0, sizeof(xInUse));
    xWebServerHandle = NULL;
    printf("Scope data system initialized (zero-copy, owned buffers)\n");
}

/*  Save task handle for notify on publish */
void vScopeDataSetWebServerHandle(TaskHandle_t handle) {
    xWebServerHandle = handle;
}

/* Compute min/max/avg in volts for a given buffer */
static void vCalculateStatistics(ScopeBuffer_t *pBuffer) {
    if (pBuffer == NULL || pBuffer->pusSamples == NULL || pBuffer->bStatsValid) return;

    uint32_t sum = 0;
    uint16_t minv = 4095, maxv = 0;

    for (uint32_t i = 0; i < ADC_BUFFER_SIZE; i++) {
        uint16_t s = pBuffer->pusSamples[i];
        sum += s;
        if (s < minv) minv = s;
        if (s > maxv) maxv = s;
    }

    /* Convert raw ADC counts to volts (assumes 3.3V reference from our Pico 3.3V output, 12-bit) */
    pBuffer->avg_voltage = ((float) sum / ADC_BUFFER_SIZE) * 3.3f / 4095.0f;
    pBuffer->min_voltage = (float) minv * 3.3f / 4095.0f;
    pBuffer->max_voltage = (float) maxv * 3.3f / 4095.0f;
    pBuffer->bStatsValid = true;
}

/* Publish completed DMA buffer
 * Called by acquisition task when a DMA buffer completes.
 * If an older "ready" buffer exists, release it back to ADC (drop older).
 * Store the new buffer into the "ready" slot and notify the web task.
 */
void vScopeDataPublishBuffer(uint16_t *buffer, uint32_t timestamp) {
    taskENTER_CRITICAL();
    /* Drop older 'ready' if present (always keep the newest) */
    if (xReady.pusSamples != NULL) {
        vAdcDmaReleaseBuffer(xReady.pusSamples);
    }

    xReady.pusSamples = buffer;
    xReady.ulTimestamp = timestamp;
    xReady.bStatsValid = false;
    taskEXIT_CRITICAL();

    /* Notify the web server task for low-latency push */
    if (xWebServerHandle != NULL) {
        xTaskNotifyGive(xWebServerHandle);
    }
}


/* Web server consumer function
 * FIXED: Only promote xReady to xInUse if xInUse was returned previously.
 * This allows the consumer to hold a buffer across multiple calls.
 */
bool bGetLatestScopeData(ScopeBuffer_t *pData, bool bCalculateStats) {
    if (pData == NULL) return false;

    ScopeBuffer_t xLocalCopy = { 0 };
    bool bHaveData = false;
    bool bNeedStats = false;

    taskENTER_CRITICAL();
    
    /* FIXED: Only promote if we have a ready buffer AND no in-use buffer
     * This lets the consumer hold the same buffer across multiple calls */
    if (xReady.pusSamples != NULL && xInUse.pusSamples == NULL) {
        /* Promote ready to in_use */
        xInUse = xReady;
        memset(&xReady, 0, sizeof(xReady));
    }

    if (xInUse.pusSamples != NULL) {
        xLocalCopy = xInUse;
        bHaveData = true;
        bNeedStats = (bCalculateStats && !xInUse.bStatsValid);
    }
    taskEXIT_CRITICAL();

    if (!bHaveData) return false;

    /* Compute stats outside the critical section to minimise lock time */
    if (bNeedStats) {
        ScopeBuffer_t xTmp = xLocalCopy;
        vCalculateStatistics(&xTmp);

        taskENTER_CRITICAL();
        if (xInUse.pusSamples == xLocalCopy.pusSamples) {
            xInUse.avg_voltage = xTmp.avg_voltage;
            xInUse.min_voltage = xTmp.min_voltage;
            xInUse.max_voltage = xTmp.max_voltage;
            xInUse.bStatsValid  = true;
            xLocalCopy = xInUse;
        } else {
            xLocalCopy = xInUse;
        }
        taskEXIT_CRITICAL();
    }

    *pData = xLocalCopy;
    return true;
}

/* Explicitly release the in-use buffer */
void vScopeDataReleaseBuffer(void) {
    taskENTER_CRITICAL();
    if (xInUse.pusSamples != NULL) {
        vAdcDmaReleaseBuffer(xInUse.pusSamples);
        memset(&xInUse, 0, sizeof(xInUse));
    }
    taskEXIT_CRITICAL();
}