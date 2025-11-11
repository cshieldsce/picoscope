#include "adc_dma.h"
#include "pico/stdlib.h"
#include "stdio.h"
#include "hardware/dma.h"
#include "hardware/adc.h"
#include "FreeRTOS.h"    
#include "task.h"
#include <string.h>

/* DMA Globals */
static int iDmaChannel;
static dma_channel_config dmaConfig;
static volatile bool bCaptureRunning = false;

/* Buffer Management */
static AdcBuffer_t xBuffers[NUM_BUFFERS];
static volatile uint8_t ucWriteIndex = 0;
static volatile uint32_t ulBufferTimestamp[NUM_BUFFERS];

/* Track last completed buffer explicitly for safe handout */
static volatile uint8_t ucLastCompleted = 0;

/* DMA Completion Handler 
 * Gets called with interupt when a DMA transfer completes.
 * Checks which buffer was filled and updates states accordingly.
 */
static void vDmaHandler() {
    if (dma_channel_get_irq0_status(iDmaChannel)) {
        dma_channel_acknowledge_irq0(iDmaChannel);
        
        /* Transfer is complete which means buffer is full */
        uint8_t completed = ucWriteIndex;
        xBuffers[completed].xState = BUFFER_FULL;
        ulBufferTimestamp[completed] = to_ms_since_boot(get_absolute_time());
        ucLastCompleted = completed; /* Remember which one completed */
        
        /* Get index to next buffer */
        uint8_t next = (uint8_t) ((completed + 1) % NUM_BUFFERS);

        /* Avoid overwriting a PROCESSING buffer.
         * With 3 buffers, the ISR should rarely hit this case because
         * one is FILLING, one is FULL, and one is PROCESSING.
         * If it does happen, reuse the just-completed buffer (drop older frame).
         */
        if (xBuffers[next].xState == BUFFER_PROCESSING) {
            next = completed;     /* Reuse current buffer */
            ulOverruns++;         /* Count the drop */
        }
        
        /* Update buffer state and start transfer with next buffer info */
        if (bCaptureRunning) {
            ucWriteIndex = next;
            xBuffers[ucWriteIndex].xState = BUFFER_FILLING;
            dma_channel_configure(
                iDmaChannel,
                &dmaConfig,
                xBuffers[ucWriteIndex].usData,
                &adc_hw->fifo,
                ADC_BUFFER_SIZE,
                true
            );
        }
    }
}

/* ADC DMA Initialization
 * Setups the ADC and DMA for continuous sampling.
 * Configures ADC with DREQ for DMA requests.
 * Enables DMA interupts for buffer management w/ triple buffering.
 */
void vAdcDmaInit() {
    printf("ADC_DMA: Initializing...\n");

    /* ADDED: Stop everything first if already initialized */
    if (bCaptureRunning) {
        vAdcDmaStop();
    }
    
    /* ADDED: If channel was claimed before, unclaim it */
    static bool bFirstInit = true;
    if (!bFirstInit) {
        dma_channel_unclaim(iDmaChannel);
        irq_set_enabled(DMA_IRQ_0, false);
        irq_remove_handler(DMA_IRQ_0, vDmaHandler);
    }
    bFirstInit = false;

    /* Turn off the ADC */
    adc_run(false);
    
    /* Initialize ADC GPIO for our channel */
    adc_gpio_init(26 + ADC_CHANNEL);
    
    /* Initialize the ADC */
    adc_init();
    adc_select_input(ADC_CHANNEL);
    
    /* Sets ADC to round robin countinous sampling 
       Allows us to transfer the samples to memory without continuous CPU intervention.
       Might not need for single channel.
    */
    adc_set_round_robin(0);

    /* Set sampling to full speed */
    adc_set_clkdiv(0);  
    
    // Set up voltage reference (internal 3.3V)
    adc_hw->cs |= ADC_CS_EN_BITS;
    adc_hw->cs &= ~ADC_CS_RROBIN_BITS;
    adc_hw->cs &= ~ADC_CS_AINSEL_BITS;
    
    /* Configure the FIFO */
    adc_fifo_setup(
        true,    /* Enable FIFO */
        true,    /* Enable DREQ */
        1,       /* DREQ threshold to send samples */
        false,   /* Disable error flag */
        false    /* 12-bit mode */
    );

    /* Drain FIFO just in case */
    adc_fifo_drain();
    
    iDmaChannel = dma_claim_unused_channel(true);
    configASSERT(iDmaChannel != -1);

    /* DMA Config
       Setup DMA to read from ADC FIFO and write to our buffers.
     */
    dmaConfig = dma_channel_get_default_config(iDmaChannel);

    /* 12-bits for our data and 4-bits for the error flag */
    channel_config_set_transfer_data_size(&dmaConfig, DMA_SIZE_16);

    /* Read from same ADC FIFO address */
    channel_config_set_read_increment(&dmaConfig, false);     
    
    /* Write to incrementing memory address */
    channel_config_set_write_increment(&dmaConfig, true);   

    /* ADC generates the data requests */
    channel_config_set_dreq(&dmaConfig, DREQ_ADC);                
    
    /* Enable interrupts on our channel and handler */
    dma_channel_set_irq0_enabled(iDmaChannel, true);
    irq_set_exclusive_handler(DMA_IRQ_0, vDmaHandler);
    irq_set_enabled(DMA_IRQ_0, true);

    /* Initialize buffer states */
    for(int i = 0; i < NUM_BUFFERS; i++) {
        xBuffers[i].xState = BUFFER_EMPTY;
        ulBufferTimestamp[i] = 0;     /* init timestamps */
    }

    ucWriteIndex = 0;                  /* explicit init */
    ucLastCompleted = 0;               /* init last-completed */
    ulOverruns = 0;                    /* reset overruns */
}

void vAdcDmaStartContinous() {
    /* Start continuous capture */
    bCaptureRunning = true;
    ucWriteIndex = 0;
    
    /* Begin our first transfer */
    xBuffers[ucWriteIndex].xState = BUFFER_FILLING;
    adc_run(true);
    dma_channel_configure(
        iDmaChannel,
        &dmaConfig,
        xBuffers[ucWriteIndex].usData,
        &adc_hw->fifo,
        ADC_BUFFER_SIZE,
        true /* Start immediately */
    );
}

void vAdcDmaStop() {
    if (!bCaptureRunning) return;
    
    printf("ADC_DMA: Stopping...\n");
    
    /* Stop ADC conversion */
    adc_run(false);
    
    /* Disable DMA channel */
    dma_channel_abort(iDmaChannel);
    
    /* Clear any pending IRQs */
    dma_channel_acknowledge_irq0(iDmaChannel);
    
    /* Drain FIFO */
    adc_fifo_drain();
    
    bCaptureRunning = false;
    
    /* ADDED: Reset buffer states */
    taskENTER_CRITICAL();
    for (int i = 0; i < NUM_BUFFERS; i++) {
        xBuffers[i].xState = BUFFER_EMPTY;
        ulBufferTimestamp[i] = 0;
    }
    ucWriteIndex = 0;
    ucLastCompleted = 0;
    taskEXIT_CRITICAL();
}

/* Zero-copy version: Returns pointer to DMA buffer (setting it to PROCESSING) */
bool bAdcDmaGetLatestBufferPtr(uint16_t** pusBufferPtr, uint32_t* pulTimestamp) {
    if (pusBufferPtr == NULL || pulTimestamp == NULL) return false;

    bool ok = false;

    /* CHANGE: Use a critical section to avoid races with ISR and use ucLastCompleted */
    taskENTER_CRITICAL();
    uint8_t latest = ucLastCompleted;

    if (xBuffers[latest].xState == BUFFER_FULL) {
        xBuffers[latest].xState = BUFFER_PROCESSING;  /* CHANGE: transfer ownership safely */
        *pusBufferPtr = xBuffers[latest].usData;
        *pulTimestamp = ulBufferTimestamp[latest];
        ok = true;
    }
    taskEXIT_CRITICAL();

    return ok;
}

/* Release a previously handed-out DMA buffer (setting it to EMPTY) */
void vAdcDmaReleaseBuffer(uint16_t* pusBufferPtr) {
    if (pusBufferPtr == NULL) return;

    /* CHANGE: Protect against ISR while changing state */
    taskENTER_CRITICAL();
    for (int i = 0; i < NUM_BUFFERS; i++) {
        if (xBuffers[i].usData == pusBufferPtr && xBuffers[i].xState == BUFFER_PROCESSING) {
            xBuffers[i].xState = BUFFER_EMPTY;
            break;
        }
    }
    taskEXIT_CRITICAL();
}