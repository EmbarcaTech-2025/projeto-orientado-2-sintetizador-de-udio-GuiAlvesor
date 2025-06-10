#ifndef BUZZER_H
#define BUZZER_H

#include "pico/stdlib.h"

void pwm_init_buzzer(uint pin);
void beep(uint pin, uint duration_ms);
void beep_variable_freq(uint pin, uint freq, uint duration_ms);

#endif
