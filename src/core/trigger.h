#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifndef DISPLAY_POINTS
#define DISPLAY_POINTS 256
#endif

/*
 * Trigger configuration and result types.
 *
 * - fTimePerDivMs: UI "time/div" in milliseconds; there are 10 divisions in the span
 *   used by the trigger code (so effective span_ms = fTimePerDivMs * 10).
 * - fPretriggerFrac: fraction of the span placed before the trigger point (0.0..0.9).
 * - uLevelCounts: ADC counts for trigger level (0..4095).
 * - uHysteresis: counts used to create a band [level-hyst .. level+hyst] to avoid chatter.
 *
 * The API maps raw ADC buffers into DISPLAY_POINTS output samples. The exact
 * resampling/decimation strategy may use min/max averaging or linear resampling
 * depending on cfg->eSmoothing and code path.
 */

typedef enum {
    TRIG_MODE_AUTO = 0,     // If no edge found, fall back to centered window
    TRIG_MODE_NORMAL,       // Require edge, otherwise keep last good or skip
    TRIG_MODE_NONE          // No alignment, just show the latest window
} TriggerMode_e;

typedef enum {
    TRIG_EDGE_RISING = 0,
    TRIG_EDGE_FALLING
} TriggerEdge_e;

typedef enum {
    SMOOTH_MINMAX = 0,      // Min/Max pair per bin (good for spikes)
    SMOOTH_AVERAGE          // Simple boxcar average per bin
} Smoothing_e;

typedef struct {
    // Triggering
    TriggerMode_e  eMode;
    TriggerEdge_e  eEdge;
    uint16_t       uLevelCounts;     // 0..4095 (12-bit ADC)
    uint16_t       uHysteresis;      // counts around level to avoid chatter

    // Timebase
    float          fTimePerDivMs;    // UI “time/div”, span is 10 divisions
    float          fPretriggerFrac;  // 0.0 .. 0.9, fraction of span before trigger

    // Rendering
    Smoothing_e    eSmoothing;
} TriggerConfig_t;

typedef struct {
    // Where we triggered in the input buffer (-1 if not found)
    int            iTriggerIndex;
    // Input window used (start, length in samples, clamped to input)
    uint32_t       uStart;
    uint32_t       uLen;
    // Output count actually written to dst (normally DISPLAY_POINTS)
    uint32_t       uOutCount;
    // True if an edge was found and used
    bool           bTriggered;
} TriggerResult_t;

/* Initialize with sensible defaults */
void vTriggerInitDefault(TriggerConfig_t* pxCfg);

/* Build a decimated frame, aligned to trigger if possible.
 * - pusSrc:     pointer to raw ADC samples
 * - ulSrcLen:   number of samples in src
 * - ulFs_hz:    sample rate (used for timebase); if 0, uses src_len as span
 * - pxCfg:      trigger/timebase/smoothing configuration
 * - pusDst:     output array for DISPLAY_POINTS samples (decimated)
 * - ulDstLen:   length of dst (use DISPLAY_POINTS)
 * - pxOut:      optional result info (can be NULL)
 * Returns true if dst was filled successfully.
 */
bool bTriggerBuildFrame(const uint16_t* pusSrc, uint32_t ulSrcLen, uint32_t ulFs_hz,
                        const TriggerConfig_t* pxCfg, uint16_t* pusDst, uint32_t ulDstLen,
                        TriggerResult_t* pxOut);