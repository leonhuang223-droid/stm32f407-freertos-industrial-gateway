#include "f407_gt911_port.h"

#include "FreeRTOS.h"
#include "main.h"
#include "task.h"

#include <stddef.h>

#define GT911_SCL_PORT GPIOD
#define GT911_SCL_PIN GPIO_PIN_7
#define GT911_SDA_PORT GPIOD
#define GT911_SDA_PIN GPIO_PIN_3
#define GT911_RESET_PORT GPIOD
#define GT911_RESET_PIN GPIO_PIN_6
#define GT911_INT_PORT GPIOG
#define GT911_INT_PIN GPIO_PIN_8
#define GT911_NOTIFY_VALUE 1u

typedef struct {
    TaskHandle_t owner_task;
    volatile uint8_t interrupt_pending;
    uint8_t initialized;
} f407_gt911_context_t;

static f407_gt911_context_t gt911_context;
static i2c_bus_t gt911_bus;

static void delay_us(uint32_t microseconds)
{
    const uint32_t ticks_per_us = SystemCoreClock / 1000000u;
    const uint32_t ticks = ticks_per_us * microseconds;
    const uint32_t start = DWT->CYCCNT;

    while ((uint32_t)(DWT->CYCCNT - start) < ticks) {
    }
}

static void delay_ms(void *context, uint32_t milliseconds)
{
    (void)context;
    if (milliseconds == 0u) {
        return;
    }
    if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
        vTaskDelay(pdMS_TO_TICKS(milliseconds));
    } else {
        HAL_Delay(milliseconds);
    }
}

static void scl_write(uint8_t high)
{
    GT911_SCL_PORT->BSRR = high != 0u
        ? GT911_SCL_PIN : (uint32_t)GT911_SCL_PIN << 16u;
}

static void sda_write(uint8_t high)
{
    GT911_SDA_PORT->BSRR = high != 0u
        ? GT911_SDA_PIN : (uint32_t)GT911_SDA_PIN << 16u;
}

static uint8_t sda_read(void)
{
    return (GT911_SDA_PORT->IDR & GT911_SDA_PIN) != 0u ? 1u : 0u;
}

static status_t wait_scl_high(void)
{
    unsigned int attempt;

    scl_write(1u);
    for (attempt = 0u; attempt < 1000u; ++attempt) {
        if ((GT911_SCL_PORT->IDR & GT911_SCL_PIN) != 0u) {
            return SYS_OK;
        }
        delay_us(1u);
    }
    return ERR_TIMEOUT;
}

static status_t i2c_start(void)
{
    sda_write(1u);
    if (wait_scl_high() != SYS_OK) {
        return ERR_TIMEOUT;
    }
    delay_us(3u);
    sda_write(0u);
    delay_us(3u);
    scl_write(0u);
    return SYS_OK;
}

static void i2c_stop(void)
{
    sda_write(0u);
    delay_us(2u);
    scl_write(1u);
    delay_us(3u);
    sda_write(1u);
    delay_us(3u);
}

static status_t i2c_write_byte(uint8_t value)
{
    unsigned int bit;

    for (bit = 0u; bit < 8u; ++bit) {
        sda_write((value & 0x80u) != 0u ? 1u : 0u);
        delay_us(2u);
        if (wait_scl_high() != SYS_OK) {
            return ERR_TIMEOUT;
        }
        delay_us(3u);
        scl_write(0u);
        value <<= 1u;
    }
    sda_write(1u);
    delay_us(2u);
    if (wait_scl_high() != SYS_OK) {
        return ERR_TIMEOUT;
    }
    delay_us(2u);
    if (sda_read() != 0u) {
        scl_write(0u);
        return ERR_IO;
    }
    scl_write(0u);
    return SYS_OK;
}

static status_t i2c_read_byte(uint8_t *value, uint8_t acknowledge)
{
    unsigned int bit;
    uint8_t received = 0u;

    sda_write(1u);
    for (bit = 0u; bit < 8u; ++bit) {
        received <<= 1u;
        if (wait_scl_high() != SYS_OK) {
            return ERR_TIMEOUT;
        }
        delay_us(2u);
        received |= sda_read();
        scl_write(0u);
        delay_us(2u);
    }
    sda_write(acknowledge != 0u ? 0u : 1u);
    delay_us(2u);
    if (wait_scl_high() != SYS_OK) {
        return ERR_TIMEOUT;
    }
    delay_us(2u);
    scl_write(0u);
    sda_write(1u);
    *value = received;
    return SYS_OK;
}

static status_t send_address(uint8_t address, uint8_t read)
{
    return i2c_write_byte((uint8_t)((address << 1u) |
                           (read != 0u ? 1u : 0u)));
}

static status_t write_bytes(uint8_t address, const uint8_t *data,
                            size_t length, uint8_t send_stop)
{
    size_t i;
    status_t status = i2c_start();

    if (status == SYS_OK) {
        status = send_address(address, 0u);
    }
    for (i = 0u; status == SYS_OK && i < length; ++i) {
        status = i2c_write_byte(data[i]);
    }
    if (send_stop != 0u || status != SYS_OK) {
        i2c_stop();
    }
    return status;
}

static status_t read_bytes(uint8_t address, uint8_t *data, size_t length)
{
    size_t i;
    status_t status = i2c_start();

    if (status == SYS_OK) {
        status = send_address(address, 1u);
    }
    for (i = 0u; status == SYS_OK && i < length; ++i) {
        status = i2c_read_byte(&data[i], i + 1u < length ? 1u : 0u);
    }
    i2c_stop();
    return status;
}

static status_t bus_write(void *context, uint8_t address,
                          const uint8_t *data, size_t length)
{
    (void)context;
    if (data == 0 || length == 0u) {
        return ERR_INVALID_ARG;
    }
    return write_bytes(address, data, length, 1u);
}

static status_t bus_read(void *context, uint8_t address,
                         uint8_t *data, size_t length)
{
    (void)context;
    if (data == 0 || length == 0u) {
        return ERR_INVALID_ARG;
    }
    return read_bytes(address, data, length);
}

static status_t bus_write_read(void *context, uint8_t address,
                               const uint8_t *write_data,
                               size_t write_length, uint8_t *read_data,
                               size_t read_length)
{
    status_t status;

    (void)context;
    if (write_data == 0 || read_data == 0 || write_length == 0u ||
        read_length == 0u) {
        return ERR_INVALID_ARG;
    }
    status = write_bytes(address, write_data, write_length, 0u);
    if (status == SYS_OK) {
        status = read_bytes(address, read_data, read_length);
    }
    return status;
}

static void configure_interrupt_input(void)
{
    GPIO_InitTypeDef gpio = { 0 };

    gpio.Pin = GT911_INT_PIN;
    gpio.Mode = GPIO_MODE_IT_FALLING;
    gpio.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GT911_INT_PORT, &gpio);
    __HAL_GPIO_EXTI_CLEAR_IT(GT911_INT_PIN);
    HAL_NVIC_SetPriority(EXTI9_5_IRQn, 6u, 0u);
    HAL_NVIC_EnableIRQ(EXTI9_5_IRQn);
}

static status_t select_address(void *context, uint8_t address)
{
    GPIO_InitTypeDef gpio = { 0 };

    (void)context;
    HAL_NVIC_DisableIRQ(EXTI9_5_IRQn);
    HAL_GPIO_WritePin(GT911_RESET_PORT, GT911_RESET_PIN, GPIO_PIN_RESET);
    gpio.Pin = GT911_INT_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GT911_INT_PORT, &gpio);
    HAL_GPIO_WritePin(GT911_INT_PORT, GT911_INT_PIN,
                      address == GT911_DEFAULT_ADDRESS
                          ? GPIO_PIN_RESET : GPIO_PIN_SET);
    delay_ms(0, 10u);
    HAL_GPIO_WritePin(GT911_RESET_PORT, GT911_RESET_PIN, GPIO_PIN_SET);
    delay_ms(0, 6u);
    configure_interrupt_input();
    delay_ms(0, 50u);
    gt911_context.interrupt_pending = 0u;
    return SYS_OK;
}

static status_t wake_controller(void *context)
{
    GPIO_InitTypeDef gpio = { 0 };

    (void)context;
    HAL_NVIC_DisableIRQ(EXTI9_5_IRQn);
    gpio.Pin = GT911_INT_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GT911_INT_PORT, &gpio);
    HAL_GPIO_WritePin(GT911_INT_PORT, GT911_INT_PIN, GPIO_PIN_RESET);
    delay_ms(0, 5u);
    configure_interrupt_input();
    delay_ms(0, 50u);
    return SYS_OK;
}

static void configure_gpio(void)
{
    GPIO_InitTypeDef gpio = { 0 };

    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0u;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    HAL_GPIO_WritePin(GPIOD, GT911_SCL_PIN | GT911_SDA_PIN,
                      GPIO_PIN_SET);
    gpio.Pin = GT911_SCL_PIN | GT911_SDA_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_OD;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    HAL_GPIO_Init(GPIOD, &gpio);

    HAL_GPIO_WritePin(GT911_RESET_PORT, GT911_RESET_PIN, GPIO_PIN_RESET);
    gpio.Pin = GT911_RESET_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GT911_RESET_PORT, &gpio);
}

status_t f407_gt911_port_construct(gt911_t *device)
{
    static const i2c_bus_ops_t bus_ops = {
        bus_write, bus_read, bus_write_read, delay_ms
    };
    static const gt911_io_ops_t io_ops = {
        select_address, wake_controller
    };
    static const gt911_config_t config = {
        GT911_DEFAULT_ADDRESS, 800u, 480u, 0u, 0u, 0u
    };
    status_t status;

    if (device == 0) {
        return ERR_INVALID_ARG;
    }
    configure_gpio();
    gt911_context.initialized = 1u;
    status = i2c_bus_construct(&gt911_bus, &bus_ops, &gt911_context, 20u);
    if (status == SYS_OK) {
        status = gt911_construct(device, &gt911_bus, &io_ops,
                                 &gt911_context, &config);
    }
    return status;
}

void f407_gt911_port_bind_current_task(void)
{
    gt911_context.owner_task = xTaskGetCurrentTaskHandle();
}

uint8_t f407_gt911_port_take_interrupt(void)
{
    uint8_t pending;

    taskENTER_CRITICAL();
    pending = gt911_context.interrupt_pending;
    gt911_context.interrupt_pending = 0u;
    taskEXIT_CRITICAL();
    return pending;
}

void EXTI9_5_IRQHandler(void)
{
    BaseType_t should_yield = pdFALSE;

    if (__HAL_GPIO_EXTI_GET_IT(GT911_INT_PIN) != RESET) {
        __HAL_GPIO_EXTI_CLEAR_IT(GT911_INT_PIN);
        gt911_context.interrupt_pending = 1u;
        if (gt911_context.initialized != 0u &&
            gt911_context.owner_task != 0) {
            (void)xTaskNotifyFromISR(gt911_context.owner_task,
                                    GT911_NOTIFY_VALUE, eSetBits,
                                    &should_yield);
        }
    }
    portYIELD_FROM_ISR(should_yield);
}
