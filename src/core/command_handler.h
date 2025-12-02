#ifndef COMMAND_HANDLER_H
#define COMMAND_HANDLER_H

#include <stdint.h>
#include <stdbool.h>
#include "trigger.h"

// Command types matching oscilloscope subsystems
typedef enum {
    CMD_TRIGGER_MODE,      // AUTO/NORMAL/SINGLE
    CMD_TRIGGER_EDGE,      // RISING/FALLING
    CMD_TRIGGER_LEVEL,     // Voltage level
    CMD_TIMEBASE_SCALE,    // Time/div (10μs to 1s)
    CMD_VERTICAL_SCALE,    // Volts/div (future)
    CMD_SAMPLE_RATE,       // Set acquisition rate
    CMD_RUN_STOP,          // Start/stop capture
    CMD_GET_STATUS         // Query current config
} CommandType_e;

// Command packet from browser (JSON -> struct)
typedef struct {
    CommandType_e eType;
    union {
        TriggerMode_e  eTriggerMode;
        TriggerEdge_e  eTriggerEdge;
        float          fTriggerLevel;    // Volts
        float          fTimePerDiv;      // Seconds
        uint32_t       ulSampleRate;     // Hz
        bool           bRunning;
    } uValue;
} ScopeCommand_t;

// Response packet to browser (struct -> JSON)
typedef struct {
    bool            bSuccess;
    char            acMessage[64];
    TriggerConfig_t xTriggerConfig;
    uint32_t        ulSampleRate;
    bool            bRunning;
} ScopeStatus_t;

// Initialize command handler
void vCommandHandlerInit(void);

// Execute command and return status
bool bCommandHandlerExecute(const ScopeCommand_t* pxCmd, ScopeStatus_t* pxStatus);

// Get current scope configuration
void vCommandHandlerGetStatus(ScopeStatus_t* pxStatus);

TriggerConfig_t* pxCommandHandlerGetTriggerConfig(void);

#endif // COMMAND_HANDLER_H

