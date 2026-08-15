#include "f407_cli_port.h"

#include "FreeRTOS.h"
#include "task.h"
#include "usart.h"

#include <string.h>

#define F407_CLI_DMA_RX_CAPACITY 128u
#define F407_CLI_RING_CAPACITY 256u
#define F407_CLI_NOTIFY_RX (1u << 0)
#define F407_CLI_NOTIFY_TX (1u << 1)
#define F407_CLI_NOTIFY_ERROR (1u << 2)

typedef struct {
    UART_HandleTypeDef *uart;
    TaskHandle_t owner;
    uint8_t dma_rx[F407_CLI_DMA_RX_CAPACITY];
    uint8_t ring[F407_CLI_RING_CAPACITY];
    volatile uint16_t head;
    volatile uint16_t tail;
    volatile uint16_t dma_position;
    volatile uint32_t overflows;
    volatile uint8_t receive_error;
} f407_cli_context_t;

static f407_cli_context_t cli_context;

static status_t start_receive(f407_cli_context_t *context)
{
    HAL_StatusTypeDef hal_status;

    (void)HAL_UART_DMAStop(context->uart);
    context->dma_position = 0u;
    hal_status = HAL_UARTEx_ReceiveToIdle_DMA(
        context->uart, context->dma_rx, sizeof(context->dma_rx));
    if (hal_status == HAL_OK && context->uart->hdmarx != 0) {
        __HAL_DMA_DISABLE_IT(context->uart->hdmarx, DMA_IT_HT);
    }
    return hal_status == HAL_OK ? SYS_OK : ERR_IO;
}

static status_t cli_init(void *opaque)
{
    f407_cli_context_t *context = opaque;

    if (context == 0 || context->uart == 0) {
        return ERR_INVALID_ARG;
    }
    context->owner = xTaskGetCurrentTaskHandle();
    context->head = 0u;
    context->tail = 0u;
    context->receive_error = 0u;
    return start_receive(context);
}

static size_t drain_ring(f407_cli_context_t *context, uint8_t *data,
                         size_t capacity)
{
    size_t length = 0u;

    taskENTER_CRITICAL();
    while (length < capacity && context->tail != context->head) {
        data[length++] = context->ring[context->tail];
        context->tail = (uint16_t)((context->tail + 1u) %
                                   F407_CLI_RING_CAPACITY);
    }
    taskEXIT_CRITICAL();
    return length;
}

static status_t cli_read(void *opaque, uint8_t *data, size_t capacity,
                         size_t *length, uint32_t timeout_ms)
{
    f407_cli_context_t *context = opaque;
    uint32_t events = 0u;

    if (context == 0 || data == 0 || capacity == 0u || length == 0) {
        return ERR_INVALID_ARG;
    }
    if (context->receive_error != 0u) {
        context->receive_error = 0u;
        if (start_receive(context) != SYS_OK) {
            return ERR_IO;
        }
    }
    *length = drain_ring(context, data, capacity);
    if (*length != 0u) {
        return SYS_OK;
    }
    if (xTaskNotifyWait(0u, F407_CLI_NOTIFY_RX | F407_CLI_NOTIFY_ERROR,
                        &events, pdMS_TO_TICKS(timeout_ms)) != pdPASS) {
        return ERR_TIMEOUT;
    }
    if ((events & F407_CLI_NOTIFY_ERROR) != 0u) {
        return ERR_IO;
    }
    *length = drain_ring(context, data, capacity);
    return *length != 0u ? SYS_OK : ERR_TIMEOUT;
}

static status_t cli_write(void *opaque, const uint8_t *data, size_t length,
                          uint32_t timeout_ms)
{
    f407_cli_context_t *context = opaque;
    TickType_t start;
    TickType_t timeout;
    uint32_t events;

    if (context == 0 || data == 0 || length == 0u || length > UINT16_MAX) {
        return ERR_INVALID_ARG;
    }
    if (HAL_UART_Transmit_DMA(context->uart, data, (uint16_t)length) !=
        HAL_OK) {
        return ERR_IO;
    }
    start = xTaskGetTickCount();
    timeout = pdMS_TO_TICKS(timeout_ms);
    for (;;) {
        TickType_t elapsed = xTaskGetTickCount() - start;
        TickType_t remaining = elapsed < timeout ? timeout - elapsed : 0u;

        if (xTaskNotifyWait(0u, F407_CLI_NOTIFY_TX |
                           F407_CLI_NOTIFY_ERROR, &events, remaining) !=
            pdPASS) {
            (void)HAL_UART_AbortTransmit(context->uart);
            return ERR_TIMEOUT;
        }
        if ((events & F407_CLI_NOTIFY_ERROR) != 0u) {
            return ERR_IO;
        }
        if ((events & F407_CLI_NOTIFY_TX) != 0u) {
            return SYS_OK;
        }
    }
}

static status_t cli_suspend(void *opaque)
{
    f407_cli_context_t *context = opaque;

    return context != 0 && HAL_UART_DMAStop(context->uart) == HAL_OK
        ? SYS_OK : ERR_IO;
}

static status_t cli_resume(void *opaque)
{
    return start_receive(opaque);
}

static const cli_transport_ops_t cli_ops = {
    cli_init,
    cli_read,
    cli_write,
    cli_suspend,
    cli_resume
};

status_t f407_cli_configure(app_context_t *context)
{
    app_cli_config_t config;

    memset(&cli_context, 0, sizeof(cli_context));
    cli_context.uart = &huart1;
    config.transport_ops = &cli_ops;
    config.transport_context = &cli_context;
    return app_context_configure_cli(context, &config);
}

static void notify_owner(f407_cli_context_t *context, uint32_t bits)
{
    BaseType_t wake = pdFALSE;

    if (context->owner != 0) {
        xTaskNotifyFromISR(context->owner, bits, eSetBits, &wake);
        portYIELD_FROM_ISR(wake);
    }
}

static void push_byte(f407_cli_context_t *context, uint8_t byte)
{
    uint16_t next = (uint16_t)((context->head + 1u) %
                               F407_CLI_RING_CAPACITY);

    if (next == context->tail) {
        context->overflows++;
        return;
    }
    context->ring[context->head] = byte;
    __DMB();
    context->head = next;
}

void f407_cli_uart_tx_complete(UART_HandleTypeDef *handle)
{
    if (handle == cli_context.uart) {
        notify_owner(&cli_context, F407_CLI_NOTIFY_TX);
    }
}

void f407_cli_uart_rx_event(UART_HandleTypeDef *handle, uint16_t size)
{
    uint16_t position;

    if (handle != cli_context.uart || size > sizeof(cli_context.dma_rx)) {
        return;
    }
    position = cli_context.dma_position;
    if (size >= position) {
        while (position < size) {
            push_byte(&cli_context, cli_context.dma_rx[position++]);
        }
    } else {
        while (position < sizeof(cli_context.dma_rx)) {
            push_byte(&cli_context, cli_context.dma_rx[position++]);
        }
        position = 0u;
        while (position < size) {
            push_byte(&cli_context, cli_context.dma_rx[position++]);
        }
    }
    cli_context.dma_position = size == sizeof(cli_context.dma_rx)
        ? 0u : size;
    notify_owner(&cli_context, F407_CLI_NOTIFY_RX);
}

void f407_cli_uart_error(UART_HandleTypeDef *handle)
{
    if (handle == cli_context.uart) {
        cli_context.receive_error = 1u;
        notify_owner(&cli_context, F407_CLI_NOTIFY_ERROR);
    }
}
