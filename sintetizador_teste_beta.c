#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "hardware/dma.h"
#include "hardware/pwm.h"
#include "hardware/irq.h"
#include "hardware/clocks.h"
#include "hardware/i2c.h"
#include "inc/ssd1306.h"  // Assumindo biblioteca SSD1306 disponível

#define MIC_CHANNEL 2
#define MIC_PIN 28
#define BUTTON_A 5
#define BUTTON_B 6
#define BUZZER_PIN 21

#define LED_GREEN 11
#define LED_BLUE 12
#define LED_RED 13

#define ADC_CLOCK_DIV 2000
#define ADC_SAMPLE_RATE (48000000 / ADC_CLOCK_DIV)
#define RECORD_TIME 3
#define TOTAL_SAMPLES (RECORD_TIME * ADC_SAMPLE_RATE)
#define DMA_BLOCK_SIZE (ADC_SAMPLE_RATE / 4)

#define PWM_WRAP 1023

#define I2C_PORT i2c1
#define I2C_SDA 14
#define I2C_SCL 15
#define SSD1306_I2C_FREQ 100000

uint8_t ssd[ssd1306_buffer_length];
struct render_area frame_area = {
    .start_column = 0,
    .end_column = ssd1306_width - 1,
    .start_page = 0,
    .end_page = ssd1306_n_pages - 1
};

uint dma_chan;
dma_channel_config dma_cfg;

uint16_t buffer[TOTAL_SAMPLES];

volatile int buffer_pos = 0;
volatile int blocks_ready = 0;
volatile bool recording = false;
volatile bool playing = false;

uint pwm_slice_num;

void init_adc();
void init_dma();
void init_pwm_buzzer();
void init_buttons();
void start_recording();
void stop_recording();
void start_playback();
void dma_handler();
bool play_callback(struct repeating_timer *t);
void oled_init();
void init_rgb();
void set_led_recording();
void set_led_playing();
void set_led_off();
void draw_column(uint8_t *buffer, int x, int height);
void update_bars_display(uint16_t *samples, int num_samples);

int main() {
    stdio_init_all();
    init_rgb();
    oled_init();
    init_adc();
    init_dma();
    init_pwm_buzzer();
    init_buttons();

    while (true) {
        if (!recording && !playing) {
            if (!gpio_get(BUTTON_A)) {
                printf("Iniciando gravação\n");
                start_recording();
                sleep_ms(300);
            }
            if (!gpio_get(BUTTON_B)) {
                printf("Iniciando reprodução\n");
                start_playback();
                sleep_ms(300);
            }
        }

        if (recording && blocks_ready > 0) {
            int start = buffer_pos - DMA_BLOCK_SIZE;
            if (start < 0) start = 0;

            int num_samples = DMA_BLOCK_SIZE;
            if (num_samples > ssd1306_width) num_samples = ssd1306_width;

            update_bars_display(&buffer[start], num_samples);
            blocks_ready = 0;
        }

        sleep_ms(100);
    }
}

void init_adc() {
    adc_init();
    adc_gpio_init(MIC_PIN);
    adc_select_input(MIC_CHANNEL);
    adc_fifo_setup(true, true, 1, false, false);
    adc_set_clkdiv(ADC_CLOCK_DIV);
}

void init_dma() {
    dma_chan = dma_claim_unused_channel(true);
    dma_cfg = dma_channel_get_default_config(dma_chan);

    channel_config_set_transfer_data_size(&dma_cfg, DMA_SIZE_16);
    channel_config_set_read_increment(&dma_cfg, false);
    channel_config_set_write_increment(&dma_cfg, true);
    channel_config_set_dreq(&dma_cfg, DREQ_ADC);

    dma_channel_set_irq0_enabled(dma_chan, true);
    irq_set_exclusive_handler(DMA_IRQ_0, dma_handler);
    irq_set_enabled(DMA_IRQ_0, true);
}

void init_pwm_buzzer() {
    gpio_set_function(BUZZER_PIN, GPIO_FUNC_PWM);
    pwm_slice_num = pwm_gpio_to_slice_num(BUZZER_PIN);
    pwm_config config = pwm_get_default_config();
    pwm_config_set_wrap(&config, PWM_WRAP);
    pwm_init(pwm_slice_num, &config, true);
    pwm_set_gpio_level(BUZZER_PIN, 0);
}

void init_buttons() {
    gpio_init(BUTTON_A);
    gpio_set_dir(BUTTON_A, GPIO_IN);
    gpio_pull_up(BUTTON_A);

    gpio_init(BUTTON_B);
    gpio_set_dir(BUTTON_B, GPIO_IN);
    gpio_pull_up(BUTTON_B);
}

void start_recording() {
    if (recording) return;

    recording = true;
    buffer_pos = 0;
    blocks_ready = 0;

    adc_fifo_drain();
    adc_run(false);

    dma_hw->ints0 = 1u << dma_chan;
    dma_channel_configure(dma_chan, &dma_cfg,
        buffer,
        &adc_hw->fifo,
        DMA_BLOCK_SIZE,
        false);

    adc_run(true);
    dma_channel_start(dma_chan);

    set_led_recording();
}

void stop_recording() {
    if (!recording) return;

    dma_channel_abort(dma_chan);
    adc_run(false);

    recording = false;
    printf("Gravação finalizada\n");

    set_led_off();
}

void dma_handler() {
    dma_hw->ints0 = 1u << dma_chan;

    buffer_pos += DMA_BLOCK_SIZE;
    blocks_ready++;

    if (buffer_pos >= TOTAL_SAMPLES) {
        stop_recording();
    } else {
        dma_channel_configure(dma_chan, &dma_cfg,
            buffer + buffer_pos,
            &adc_hw->fifo,
            DMA_BLOCK_SIZE,
            true);
    }
}

volatile int play_pos = 0;
repeating_timer_t play_timer;

void play_sample(uint16_t sample) {
    float normalized = (float)sample / 4095.0f;
    int level = (int)(normalized * PWM_WRAP * 1.5f);
    if (level > PWM_WRAP) level = PWM_WRAP;
    pwm_set_gpio_level(BUZZER_PIN, level);
}

bool play_callback(struct repeating_timer *t) {
    if (play_pos >= buffer_pos) {
        pwm_set_gpio_level(BUZZER_PIN, 0);
        playing = false;
        set_led_off();
        return false;
    }
    play_sample(buffer[play_pos]);
    play_pos++;
    return true;
}

void start_playback() {
    if (playing || recording) return;
    if (buffer_pos == 0) {
        printf("Nenhum áudio gravado ainda.\n");
        return;
    }

    play_pos = 0;
    playing = true;

    int delay_us = 1000000 / ADC_SAMPLE_RATE;
    add_repeating_timer_us(-delay_us, play_callback, NULL, &play_timer);

    set_led_playing();
}

void init_rgb() {
    gpio_init(LED_BLUE);
    gpio_init(LED_GREEN);
    gpio_init(LED_RED);
    gpio_set_dir(LED_BLUE, GPIO_OUT);
    gpio_set_dir(LED_GREEN, GPIO_OUT);
    gpio_set_dir(LED_RED, GPIO_OUT);

    gpio_put(LED_BLUE, 0);
    gpio_put(LED_GREEN, 0);
    gpio_put(LED_RED, 0);
}

void set_led_recording() {
    gpio_put(LED_RED, 1);
    gpio_put(LED_GREEN, 0);
    gpio_put(LED_BLUE, 0);
}

void set_led_playing() {
    gpio_put(LED_RED, 0);
    gpio_put(LED_GREEN, 1);
    gpio_put(LED_BLUE, 0);
}

void set_led_off() {
    gpio_put(LED_RED, 0);
    gpio_put(LED_GREEN, 0);
    gpio_put(LED_BLUE, 0);
}

void oled_init() {
    i2c_init(I2C_PORT, SSD1306_I2C_FREQ);
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);

    ssd1306_init();

    memset(ssd, 0xFF, ssd1306_buffer_length);
    render_on_display(ssd, &frame_area);

    memset(ssd, 0x00, ssd1306_buffer_length);
    render_on_display(ssd, &frame_area);
}

void draw_column(uint8_t *buffer, int x, int height) {
    for (int page = 0; page < ssd1306_n_pages; page++) {
        uint8_t byte = 0;
        for (int bit = 0; bit < 8; bit++) {
            int y = page * 8 + bit;
            if (y >= (ssd1306_height - height)) {
                byte |= (1 << bit);
            }
        }
        buffer[page * ssd1306_width + x] = byte;
    }
}

void update_bars_display(uint16_t *samples, int num_samples) {
    memset(ssd, 0x00, ssd1306_buffer_length);

    int samples_per_col = num_samples / ssd1306_width;
    if (samples_per_col == 0) samples_per_col = 1;

    for (int x = 0; x < ssd1306_width; x++) {
        int32_t sum = 0;
        for (int j = 0; j < samples_per_col; j++) {
            int index = x * samples_per_col + j;
            if (index < num_samples) {
                int32_t centered = samples[index] - 2048;
                sum += abs(centered);
            }
        }
        int avg = sum / samples_per_col;
        int height = avg * ssd1306_height / 2048;
        if (height > ssd1306_height) height = ssd1306_height;

        draw_column(ssd, x, height);
    }

    render_on_display(ssd, &frame_area);
}