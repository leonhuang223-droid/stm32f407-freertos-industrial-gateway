#include "platform_f407.h"

#include "dma.h"
#include "gpio.h"
#include "main.h"
#include "spi.h"
#include "usart.h"

#if defined(FIRMWARE_USE_FREERTOS)
#include "FreeRTOS.h"
#include "power_manager.h"
#include "watchdog_device.h"
#endif

#ifndef FIRMWARE_VECTOR_OFFSET
#define FIRMWARE_VECTOR_OFFSET 0U
#endif

#if defined(FIRMWARE_USE_FREERTOS)
static power_manager_t *platform_power;
static watchdog_device_t *platform_watchdog;
static uint32_t planned_sleep_ms;
#endif

static void system_clock_config(void)
{
    RCC_OscInitTypeDef oscillator = { 0 };
    RCC_ClkInitTypeDef clocks = { 0 };

    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

    oscillator.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    oscillator.HSEState = RCC_HSE_ON;
    oscillator.PLL.PLLState = RCC_PLL_ON;
    oscillator.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    oscillator.PLL.PLLM = 25U;
    oscillator.PLL.PLLN = 336U;
    oscillator.PLL.PLLP = RCC_PLLP_DIV2;
    oscillator.PLL.PLLQ = 7U;
    if (HAL_RCC_OscConfig(&oscillator) != HAL_OK) {
        platform_f407_panic();
    }

    clocks.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                       RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clocks.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clocks.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clocks.APB1CLKDivider = RCC_HCLK_DIV4;
    clocks.APB2CLKDivider = RCC_HCLK_DIV2;
    if (HAL_RCC_ClockConfig(&clocks, FLASH_LATENCY_5) != HAL_OK) {
        platform_f407_panic();
    }
}

status_t platform_f407_init(void)
{
    SCB->VTOR = FLASH_BASE + FIRMWARE_VECTOR_OFFSET;
    __DSB();
    __ISB();

    HAL_Init();
    system_clock_config();
    MX_GPIO_Init();
    MX_DMA_Init();
    MX_SPI1_Init();
    MX_USART1_UART_Init();
    return SYS_OK;
}

void platform_f407_panic(void)
{
    __disable_irq();
    HAL_GPIO_WritePin(LED_R_GPIO_Port, LED_R_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LED_G_GPIO_Port, LED_G_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(LED_B_GPIO_Port, LED_B_Pin, GPIO_PIN_SET);
    for (;;) {
        __WFI();
    }
}

void Error_Handler(void)
{
    platform_f407_panic();
}

void platform_assert_panic(const char *file, int line)
{
    (void)file;
    (void)line;
    platform_f407_panic();
}

void platform_f407_pre_sleep(uint32_t *expected_idle_ticks)
{
    if (expected_idle_ticks == 0) {
        platform_f407_panic();
    }
#if defined(FIRMWARE_USE_FREERTOS)
    if (platform_power != 0 && platform_watchdog != 0) {
        uint32_t allowed_ms = 0u;
        uint32_t requested_ms = *expected_idle_ticks * portTICK_PERIOD_MS;

        if (power_manager_prepare_tickless(
                platform_power, requested_ms,
                watchdog_device_remaining_ms(platform_watchdog),
                &allowed_ms) != SYS_OK) {
            *expected_idle_ticks = 0u;
            planned_sleep_ms = 0u;
            return;
        }
        *expected_idle_ticks = allowed_ms / portTICK_PERIOD_MS;
        planned_sleep_ms = allowed_ms;
    }
#endif
}

void platform_f407_post_sleep(uint32_t expected_idle_ticks)
{
    (void)expected_idle_ticks;
#if defined(FIRMWARE_USE_FREERTOS)
    if (platform_power != 0 && planned_sleep_ms != 0u) {
        power_wake_reason_t reason =
            (SCB->ICSR & SCB_ICSR_PENDSTSET_Msk) != 0u
                ? POWER_WAKE_SYSTICK : POWER_WAKE_INTERRUPT;

        power_manager_record_wake(platform_power, planned_sleep_ms,
                                  reason);
        planned_sleep_ms = 0u;
    }
#endif
}

#if defined(FIRMWARE_USE_FREERTOS)
void platform_f407_bind_reliability(power_manager_t *power,
                                    watchdog_device_t *watchdog)
{
    platform_power = power;
    platform_watchdog = watchdog;
}
#endif
