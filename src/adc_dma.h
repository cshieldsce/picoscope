#ifndef ADC_DMA_H
#define ADC_DMA_H

#include <stdint.h>
#include "hardware/adc.h"

#define ADC_CHANNEL         0      /* ADC channel (GPIO26) */
#define ADC_PIN            26      /* Raspberry Pico 2 W GPIO pin number for ADC0 */
#define ADC_BUFFER_SIZE    1024    /* Number of samples per buffer */
#define NUM_BUFFERS        2       /* Double buffering */

/* Buffer states */
typedef enum {
    BUFFER_EMPTY = 0,
    BUFFER_FILLING,
    BUFFER_FULL,
    BUFFER_PROCESSING
} xBufferState_t;

/* Buffer structure */
typedef struct {
    uint16_t data[ADC_BUFFER_SIZE];
    xBufferState_t state;
} xAdcBuffer_t;

void vAdcDmaInit(void);
void vAdcDmaStartContinous(void);
void vAdcDmaStop(void);
bool bAdcDmaGetLatestBuffer(uint16_t* dest, uint32_t* timestamp);

#endif
