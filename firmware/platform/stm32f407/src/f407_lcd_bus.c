#include "f407_lcd_bus.h"

#include "FreeRTOS.h"
#include "main.h"
#include "task.h"

#include <stddef.h>

#define F407_LCD_COMMAND_ADDRESS ((volatile uint16_t *)0x68000000u)
#define F407_LCD_DATA_ADDRESS ((volatile uint16_t *)0x68000002u)
#define F407_SRAM_BASE ((volatile uint16_t *)0x6C000000u)
#define F407_SRAM_HALFWORD_COUNT (1024u * 1024u / sizeof(uint16_t))
#define F407_PWM_PERIOD_COUNTS 1000u

typedef struct {
    uint8_t hardware_ready;
} f407_lcd_bus_context_t;

static f407_lcd_bus_context_t lcd_context;

static void delay_ms(void *context, uint32_t delay)
{
    (void)context;
    if (delay == 0u) {
        return;
    }
    if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
        vTaskDelay(pdMS_TO_TICKS(delay));
    } else {
        HAL_Delay(delay);
    }
}

static status_t write_command(void *context, uint16_t command)
{
    f407_lcd_bus_context_t *lcd = context;

    if (lcd == 0 || lcd->hardware_ready == 0u) {
        return ERR_DEVICE_NOT_READY;
    }
    *F407_LCD_COMMAND_ADDRESS = command;
    __DSB();
    return SYS_OK;
}

static status_t write_data(void *context, uint16_t data)
{
    f407_lcd_bus_context_t *lcd = context;

    if (lcd == 0 || lcd->hardware_ready == 0u) {
        return ERR_DEVICE_NOT_READY;
    }
    *F407_LCD_DATA_ADDRESS = data;
    __DSB();
    return SYS_OK;
}

static status_t read_data(void *context, uint16_t *data)
{
    f407_lcd_bus_context_t *lcd = context;

    if (lcd == 0 || data == 0 || lcd->hardware_ready == 0u) {
        return ERR_DEVICE_NOT_READY;
    }
    *data = *F407_LCD_DATA_ADDRESS;
    __DSB();
    return SYS_OK;
}

static status_t
write_pixels(void *context, const uint16_t *pixels, size_t pixel_count)
{
    f407_lcd_bus_context_t *lcd = context;
    size_t i;

    if (lcd == 0 || pixels == 0 || pixel_count == 0u ||
        lcd->hardware_ready == 0u) {
        return ERR_INVALID_ARG;
    }
    for (i = 0u; i < pixel_count; ++i) {
        *F407_LCD_DATA_ADDRESS = pixels[i];
    }
    __DSB();
    return SYS_OK;
}

static void set_reset(void *context, uint8_t asserted)
{
    f407_lcd_bus_context_t *lcd = context;

    if (lcd != 0 && lcd->hardware_ready != 0u) {
        HAL_GPIO_WritePin(
            GPIOF, GPIO_PIN_11, asserted != 0u ? GPIO_PIN_RESET : GPIO_PIN_SET);
    }
}

static void configure_fsmc_gpio(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();

    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF12_FSMC;

    gpio.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_8 |
               GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12 |
               GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15;
    HAL_GPIO_Init(GPIOD, &gpio);

    gpio.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_7 | GPIO_PIN_8 | GPIO_PIN_9 |
               GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12 | GPIO_PIN_13 |
               GPIO_PIN_14 | GPIO_PIN_15;
    HAL_GPIO_Init(GPIOE, &gpio);

    gpio.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3 | GPIO_PIN_4 |
               GPIO_PIN_5 | GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_14 |
               GPIO_PIN_15;
    HAL_GPIO_Init(GPIOF, &gpio);

    gpio.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3 | GPIO_PIN_4 |
               GPIO_PIN_5 | GPIO_PIN_10 | GPIO_PIN_12;
    HAL_GPIO_Init(GPIOG, &gpio);
}

static void configure_control_gpio(void)
{
    GPIO_InitTypeDef gpio = {0};

    HAL_GPIO_WritePin(GPIOF, GPIO_PIN_11, GPIO_PIN_RESET);
    gpio.Pin = GPIO_PIN_11;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOF, &gpio);
}

static void configure_backlight_pwm(void)
{
    GPIO_InitTypeDef gpio = {0};
    uint32_t timer_clock = HAL_RCC_GetPCLK1Freq();

    __HAL_RCC_TIM14_CLK_ENABLE();
    if ((RCC->CFGR & RCC_CFGR_PPRE1) != RCC_CFGR_PPRE1_DIV1) {
        timer_clock *= 2u;
    }
    gpio.Pin = GPIO_PIN_9;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio.Alternate = GPIO_AF9_TIM14;
    HAL_GPIO_Init(GPIOF, &gpio);

    TIM14->CR1 = 0u;
    TIM14->PSC = timer_clock / 1000000u - 1u;
    TIM14->ARR = F407_PWM_PERIOD_COUNTS - 1u;
    TIM14->CCR1 = 0u;
    TIM14->CCMR1 = TIM_CCMR1_OC1M_1 | TIM_CCMR1_OC1M_2 | TIM_CCMR1_OC1PE;
    TIM14->CCER = TIM_CCER_CC1E | TIM_CCER_CC1P;
    TIM14->EGR = TIM_EGR_UG;
    TIM14->CR1 = TIM_CR1_ARPE | TIM_CR1_CEN;
}

static void configure_fsmc_banks(void)
{
    __HAL_RCC_FSMC_CLK_ENABLE();

    FSMC_Bank1->BTCR[4] = FSMC_BCR1_MWID_0 | FSMC_BCR1_WREN;
    FSMC_Bank1->BTCR[5] =
        (2u << FSMC_BTR1_ADDSET_Pos) | (8u << FSMC_BTR1_DATAST_Pos);
    FSMC_Bank1->BTCR[4] |= FSMC_BCR1_MBKEN;

    FSMC_Bank1->BTCR[6] = FSMC_BCR1_MWID_0 | FSMC_BCR1_WREN;
    FSMC_Bank1->BTCR[7] =
        (1u << FSMC_BTR1_ADDSET_Pos) | (5u << FSMC_BTR1_DATAST_Pos);
    FSMC_Bank1->BTCR[6] |= FSMC_BCR1_MBKEN;
}

status_t f407_lcd_bus_configure(lcd_bus_t *bus)
{
    static const lcd_bus_ops_t ops = {write_command,
                                      write_data,
                                      read_data,
                                      write_pixels,
                                      set_reset,
                                      delay_ms};

    if (bus == 0) {
        return ERR_INVALID_ARG;
    }
    configure_fsmc_gpio();
    configure_control_gpio();
    configure_backlight_pwm();
    configure_fsmc_banks();
    lcd_context.hardware_ready = 1u;
    return lcd_bus_construct(bus, &ops, &lcd_context);
}

status_t f407_lcd_backlight_set(uint8_t percent)
{
    if (lcd_context.hardware_ready == 0u) {
        return ERR_DEVICE_NOT_READY;
    }
    if (percent > 100u) {
        return ERR_INVALID_ARG;
    }
    TIM14->CCR1 = (uint32_t)percent * F407_PWM_PERIOD_COUNTS / 100u;
    return SYS_OK;
}

status_t f407_external_sram_self_test(void)
{
    static const size_t offsets[] = {0u,
                                     1u,
                                     127u,
                                     1023u,
                                     8191u,
                                     32767u,
                                     F407_SRAM_HALFWORD_COUNT / 2u,
                                     F407_SRAM_HALFWORD_COUNT - 1u};
    static const uint16_t patterns[] = {
        0x0000u, 0xFFFFu, 0xA5A5u, 0x5A5Au, 0x1357u, 0x2468u, 0x55AAu, 0xAA55u};
    uint16_t saved[sizeof(offsets) / sizeof(offsets[0])];
    size_t i;
    status_t status = SYS_OK;

    if (lcd_context.hardware_ready == 0u) {
        return ERR_DEVICE_NOT_READY;
    }
    for (i = 0u; i < sizeof(offsets) / sizeof(offsets[0]); ++i) {
        saved[i] = F407_SRAM_BASE[offsets[i]];
        F407_SRAM_BASE[offsets[i]] = patterns[i];
    }
    __DSB();
    for (i = 0u; i < sizeof(offsets) / sizeof(offsets[0]); ++i) {
        if (F407_SRAM_BASE[offsets[i]] != patterns[i]) {
            status = ERR_IO;
        }
    }
    for (i = 0u; i < sizeof(offsets) / sizeof(offsets[0]); ++i) {
        F407_SRAM_BASE[offsets[i]] = saved[i];
    }
    __DSB();
    return status;
}

uint16_t *f407_external_draw_buffer(unsigned int index)
{
    const size_t offset = (size_t)index * F407_EXTERNAL_DRAW_BUFFER_PIXELS;

    if (index > 1u ||
        offset + F407_EXTERNAL_DRAW_BUFFER_PIXELS > F407_SRAM_HALFWORD_COUNT) {
        return 0;
    }
    return (uint16_t *)&F407_SRAM_BASE[offset];
}
