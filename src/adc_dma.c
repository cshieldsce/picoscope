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
static xAdcBuffer_t buffers[NUM_BUFFERS];
static volatile uint8_t ucWriteIndex = 0;
static volatile uint8_t ucReadIndex = 0;
static volatile uint32_t ulBufferTimestamp[NUM_BUFFERS];

/* DMA Completion Handler 
 * Gets called with interupt when a DMA transfer completes.
 * Checks which buffer was filled and updates states accordingly.
 */
static void vDmaHandler() {
    if (dma_channel_get_irq0_status(iDmaChannel)) {
        dma_channel_acknowledge_irq0(iDmaChannel);
        
        /* Transfer is complete which means buffer is full */
        buffers[ucWriteIndex].state = BUFFER_FULL;
        ulBufferTimestamp[ucWriteIndex] = to_ms_since_boot(get_absolute_time());
        
        /* Get index to next buffer */
        ucWriteIndex = (ucWriteIndex + 1) % NUM_BUFFERS;
        
        /* Update buffer state and start transfer with new buffer info */
        if (bCaptureRunning) {
            buffers[ucWriteIndex].state = BUFFER_FILLING;
            dma_channel_configure(
                iDmaChannel,
                &dmaConfig,
                buffers[ucWriteIndex].data,
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
 * Enables DMA interupts for buffer management w/ double buffering.
 */
void vAdcDmaInit() {
    printf("ADC_DMA: Initializing...\n");

    /* Turn off the ADC */
    adc_run(false);
    
    /* Initialize ADC GPIO for our channel */
    adc_gpio_init(26 + ADC_CHANNEL);
    
    /* Initialize the ADC */
    adc_init();
    adc_select_input(ADC_CHANNEL);
    
    /* Sets ADC to round robin countinous sampling 
       Allows us to transfer the samples to memory without continuous CPU intervention.
    */
    adc_set_round_robin(0);

    /* Set sampling to full speed */
    adc_set_clkdiv(0);  
    
    // Set up voltage reference (internal 3.3V)
    adc_hw->cs |= ADC_CS_EN_BITS;
    adc_hw->cs &= ~ADC_CS_RROBIN_BITS;
    adc_hw->cs &= ~ADC_CS_AINSEL_BITS;
    adc_hw->cs |= ADC_CS_TS_EN_BITS;
    
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
        buffers[i].state = BUFFER_EMPTY;
    }
}

void vAdcDmaStartContinous() {
    /* Start continuous capture */
    bCaptureRunning = true;
    ucWriteIndex = 0;
    ucReadIndex = 0;
    
    /* Begin our first transfer */
    buffers[ucWriteIndex].state = BUFFER_FILLING;
    adc_run(true);
    dma_channel_configure(
        iDmaChannel,
        &dmaConfig,
        buffers[ucWriteIndex].data,
        &adc_hw->fifo,
        ADC_BUFFER_SIZE,
        true /* Start immediately */
    );
}

void vAdcDmaStop() {
    bCaptureRunning = false;
    adc_run(false);
}

bool bAdcDmaGetLatestBuffer(uint16_t* dest, uint32_t* timestamp) {
    if (buffers[ucReadIndex].state != BUFFER_FULL) {
        return false; /* Non-blocking will return if no data freeing up the CPU */
    }
    
    /* Copy buffer data at read index attach timestamp */
    memcpy(dest, buffers[ucReadIndex].data, ADC_BUFFER_SIZE * sizeof(uint16_t));
    *timestamp = ulBufferTimestamp[ucReadIndex];
    
    /* Marked the buffer as empty (will be rewrited) */
    buffers[ucReadIndex].state = BUFFER_EMPTY;

    /* Move to next buffer */
    ucReadIndex = (ucReadIndex + 1) % NUM_BUFFERS;
    
    return true;
}
