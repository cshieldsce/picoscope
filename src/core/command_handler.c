#include "command_handler.h"
#include "drivers/adc_dma.h"
#include <string.h>
#include <stdio.h>

// Global scope state (protected by mutex in real implementation)
static TriggerConfig_t xCurrentTrigger;
static uint32_t ulCurrentSampleRate = 100000;
static bool bCaptureRunning = false;

void vCommandHandlerInit(void) {
    trigger_init_default(&xCurrentTrigger);
    xCurrentTrigger.uLevelCounts = 1638;  // Your current default
    xCurrentTrigger.uHysteresis = 200;
    ulCurrentSampleRate = 100000;
    bCaptureRunning = false;
}

bool bCommandHandlerExecute(const ScopeCommand_t* pxCmd, ScopeStatus_t* pxStatus) {
    if (!pxCmd || !pxStatus) return false;
    
    memset(pxStatus, 0, sizeof(ScopeStatus_t));
    pxStatus->bSuccess = true;
    
    switch (pxCmd->eType) {
        case CMD_TRIGGER_MODE:
            xCurrentTrigger.eMode = pxCmd->uValue.eTriggerMode;
            snprintf(pxStatus->acMessage, sizeof(pxStatus->acMessage), 
                     "Trigger mode: %d", pxCmd->uValue.eTriggerMode);
            break;
            
        case CMD_TRIGGER_EDGE:
            xCurrentTrigger.eEdge = pxCmd->uValue.eTriggerEdge;
            snprintf(pxStatus->acMessage, sizeof(pxStatus->acMessage),
                     "Trigger edge: %s", 
                     pxCmd->uValue.eTriggerEdge == TRIG_EDGE_RISING ? "RISING" : "FALLING");
            break;
            
        case CMD_TRIGGER_LEVEL:
            // Convert volts to ADC counts (0-3.3V -> 0-4095)
            xCurrentTrigger.uLevelCounts = (uint16_t)(pxCmd->uValue.fTriggerLevel * 4095.0f / 3.3f);
            if (xCurrentTrigger.uLevelCounts > 4095) xCurrentTrigger.uLevelCounts = 4095;
            snprintf(pxStatus->acMessage, sizeof(pxStatus->acMessage),
                     "Trigger level: %.2fV (%u counts)", 
                     pxCmd->uValue.fTriggerLevel, xCurrentTrigger.uLevelCounts);
            break;
            
        case CMD_TIMEBASE_SCALE:
            // Time/div in seconds -> compute required sample rate
            // Target: 10 divisions across screen, DISPLAY_POINTS samples
            xCurrentTrigger.fTimePerDivMs = pxCmd->uValue.fTimePerDiv * 1000.0f;
            
            // Calculate needed sample rate to fill screen at this timebase
            float total_time_s = pxCmd->uValue.fTimePerDiv * 10.0f;
            uint32_t needed_rate = (uint32_t)(DISPLAY_POINTS / total_time_s);
            
            // Clamp to ADC limits (1kHz - 500kHz)
            if (needed_rate < 1000) needed_rate = 1000;
            if (needed_rate > 500000) needed_rate = 500000;
            
            ulCurrentSampleRate = needed_rate;
            vAdcDmaSetSampleRate(ulCurrentSampleRate);
            
            snprintf(pxStatus->acMessage, sizeof(pxStatus->acMessage),
                     "Timebase: %.1fms/div (Fs=%lu Hz)", 
                     xCurrentTrigger.fTimePerDivMs, ulCurrentSampleRate);
            break;
            
        case CMD_SAMPLE_RATE:
            ulCurrentSampleRate = pxCmd->uValue.ulSampleRate;
            vAdcDmaSetSampleRate(ulCurrentSampleRate);
            snprintf(pxStatus->acMessage, sizeof(pxStatus->acMessage),
                     "Sample rate: %lu Hz", ulCurrentSampleRate);
            break;
            
        case CMD_RUN_STOP:
            bCaptureRunning = pxCmd->uValue.bRunning;
            if (bCaptureRunning) {
                vAdcDmaStartContinous();
                snprintf(pxStatus->acMessage, sizeof(pxStatus->acMessage), "Capture started");
            } else {
                vAdcDmaStop();
                snprintf(pxStatus->acMessage, sizeof(pxStatus->acMessage), "Capture stopped");
            }
            break;
            
        case CMD_GET_STATUS:
            snprintf(pxStatus->acMessage, sizeof(pxStatus->acMessage), "Status OK");
            break;
            
        default:
            pxStatus->bSuccess = false;
            snprintf(pxStatus->acMessage, sizeof(pxStatus->acMessage), "Unknown command");
            return false;
    }
    
    // Always return current state
    pxStatus->xTriggerConfig = xCurrentTrigger;
    pxStatus->ulSampleRate = ulCurrentSampleRate;
    pxStatus->bRunning = bCaptureRunning;
    
    return true;
}

void vCommandHandlerGetStatus(ScopeStatus_t* pxStatus) {
    if (!pxStatus) return;
    memset(pxStatus, 0, sizeof(ScopeStatus_t));
    pxStatus->bSuccess = true;
    pxStatus->xTriggerConfig = xCurrentTrigger;
    pxStatus->ulSampleRate = ulCurrentSampleRate;
    pxStatus->bRunning = bAdcDmaIsRunning();  // Query actual state
    snprintf(pxStatus->acMessage, sizeof(pxStatus->acMessage), "Status OK");
}

// Getter for web server to access current trigger config
TriggerConfig_t* pxCommandHandlerGetTriggerConfig(void) {
    return &xCurrentTrigger;
}

