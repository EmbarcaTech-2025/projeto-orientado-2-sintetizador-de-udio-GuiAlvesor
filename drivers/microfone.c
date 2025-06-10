#include "headers/microfone.h"
#include "hardware/adc.h"
#include "hardware/dma.h"
#include <math.h>

// Configuração baseada em MIC_CHANNEL = 2 => GPIO28
#define MIC_CHANNEL 2
#define MIC_PIN (26 + MIC_CHANNEL)
#define SAMPLES 256

static uint16_t buffer[SAMPLES];
static int dma_chan;

void init_microfone_adc_dma() {
    adc_init();

    adc_gpio_init(MIC_PIN);
    adc_select_input(MIC_CHANNEL);

    adc_fifo_setup(
        true,    // Enable FIFO
        true,    // Enable DMA data request (DREQ)
        1,       // DREQ when at least 1 sample present
        false,
        false
    );

    dma_chan = dma_claim_unused_channel(true);
    dma_channel_config cfg = dma_channel_get_default_config(dma_chan);
    channel_config_set_transfer_data_size(&cfg, DMA_SIZE_16);
    channel_config_set_read_increment(&cfg, false);
    channel_config_set_write_increment(&cfg, true);
    channel_config_set_dreq(&cfg, DREQ_ADC);

    dma_channel_configure(
        dma_chan, 
        &cfg,
        buffer,
        &adc_hw->fifo,
        SAMPLES,
        false
    );

    adc_run(true); // Só inicia após configurar FIFO e DMA
}

uint16_t get_audio_intensity() {
    adc_fifo_drain(); // Limpa dados antigos
    dma_channel_start(dma_chan);
    while (dma_channel_is_busy(dma_chan));

    uint32_t sum = 0;
    for (int i = 0; i < SAMPLES; i++) {
        int diff = (int)buffer[i] - 2048; // Centro do range do ADC (12 bits)
        sum += diff * diff;
    }

    return (uint16_t)sqrtf(sum / (float)SAMPLES);
}
