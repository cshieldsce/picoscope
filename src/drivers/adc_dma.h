#ifndef ADC_DMA_H
#define ADC_DMA_H

#include <stdint.h>
#include "hardware/adc.h"

#define ADC_CHANNEL         0      /* ADC channel (GPIO26) */
#define ADC_PIN            26      /* Raspberry Pico 2 W GPIO pin number for ADC0 */
#define ADC_BUFFER_SIZE    1024    /* Number of samples per buffer */
#define NUM_BUFFERS        3       /* CHANGE: Triple buffering eliminates starvation */

/* Buffer states */
typedef enum {
    BUFFER_EMPTY = 0,
    BUFFER_FILLING,
    BUFFER_FULL,
    BUFFER_PROCESSING
} BufferState_t;

/* Buffer structure */
typedef struct {
    uint16_t usData[ADC_BUFFER_SIZE];
    BufferState_t xState;
} AdcBuffer_t;

/* Overrun counter when ISR has to reuse current buffer to avoid PROCESSING */
static volatile uint32_t ulOverruns = 0;

void vAdcDmaInit(void);
void vAdcDmaStartContinous(void);
void vAdcDmaStop(void);
bool bAdcDmaGetLatestBufferPtr(uint16_t** pusBufferPtr, uint32_t* pulTimestamp);

/* Release a previously handed-out DMA buffer back to the pool */
void vAdcDmaReleaseBuffer(uint16_t* pusBufferPtr);

/* Change sampling rate at runtime (Hz). Will safely restart DMA if needed. */
void vAdcDmaSetSampleRate(uint32_t ulHz);
/* Read back current target sample rate (Hz) */
uint32_t ulAdcDmaGetSampleRate(void);
/* Read back current measured sample rate (Hz) */
uint32_t ulAdcDmaGetMeasuredSampleRate(void);

/* Read back current capture status */
bool bAdcDmaIsRunning(void);

#endif