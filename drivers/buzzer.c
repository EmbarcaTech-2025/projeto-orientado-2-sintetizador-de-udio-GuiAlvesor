#include "headers/buzzer.h"
#include "hardware/pwm.h"
#include "hardware/clocks.h"

#define DEFAULT_DUTY 2048
#define PWM_WRAP 4096

void pwm_init_buzzer(uint pin) {
    gpio_set_function(pin, GPIO_FUNC_PWM);
    uint slice = pwm_gpio_to_slice_num(pin);
    pwm_config cfg = pwm_get_default_config();
    pwm_config_set_clkdiv(&cfg, clock_get_hz(clk_sys) / (100 * PWM_WRAP)); // 100Hz default
    pwm_init(slice, &cfg, true);
    pwm_set_gpio_level(pin, 0);
}

void beep(uint pin, uint duration_ms) {
    pwm_set_gpio_level(pin, DEFAULT_DUTY);
    sleep_ms(duration_ms);
    pwm_set_gpio_level(pin, 0);
    sleep_ms(100);
}

void beep_variable_freq(uint pin, uint freq, uint duration_ms) {
    uint slice = pwm_gpio_to_slice_num(pin);
    pwm_config cfg = pwm_get_default_config();
    pwm_config_set_clkdiv(&cfg, clock_get_hz(clk_sys) / (freq * PWM_WRAP));
    pwm_init(slice, &cfg, true);
    pwm_set_gpio_level(pin, DEFAULT_DUTY);
    sleep_ms(duration_ms);
    pwm_set_gpio_level(pin, 0);
}
