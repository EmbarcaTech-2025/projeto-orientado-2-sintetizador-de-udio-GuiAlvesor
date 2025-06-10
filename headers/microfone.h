#ifndef MICROFONE_H
#define MICROFONE_H

#include "pico/stdlib.h"

void init_microfone_adc_dma();
uint16_t get_audio_intensity();

#endif
