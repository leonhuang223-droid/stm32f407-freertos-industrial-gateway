#include "f407_network_port.h"

#include "f407_board_config.h"
#include "usart.h"

#include "FreeRTOS.h"
#include "task.h"

#include <limits.h>
#include <string.h>

#define NETWORK_UART_NOTIFY_TX_DONE (1u << 0)
#define NETWORK_UART_NOTIFY_RX_DONE (1u << 1)
#define NETWORK_UART_NOTIFY_ERROR (1u << 2)

typedef struct {
    UART_HandleTypeDef *handle;
    TaskHandle_t owner_task;
    volatile size_t rx_length;
} f407_network_uart_t;

static f407_network_uart_t network_uart = { &huart3, 0, 0u };

static status_t status_from_hal(HAL_StatusTypeDef status)
{
    switch (status) {
    case HAL_OK: return SYS_OK;
    case HAL_TIMEOUT: return ERR_TIMEOUT;
    case HAL_BUSY: return ERR_DEVICE_NOT_READY;
    default: return ERR_IO;
    }
}

static void clear_current_task_notification(void)
{
    uint32_t ignored;

    (void)xTaskNotifyWait(0u, UINT32_MAX, &ignored, 0u);
}

static status_t wait_for_uart_event(uint32_t required,
                                    uint32_t timeout_ms)
{
    uint32_t events = 0u;

    if (xTaskNotifyWait(0u, UINT32_MAX, &events,
                        pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        return ERR_TIMEOUT;
    }
    if ((events & NETWORK_UART_NOTIFY_ERROR) != 0u) {
        return ERR_IO;
    }
    return (events & required) != 0u ? SYS_OK : ERR_PROTOCOL;
}

static status_t network_serial_init(void *context)
{
    f407_network_uart_t *port = context;

    if (port == 0) {
        return ERR_INVALID_ARG;
    }
    MX_USART3_UART_Init();
    return port->handle->gState == HAL_UART_STATE_READY ? SYS_OK : ERR_IO;
}

static status_t network_serial_write(void *context, const uint8_t *data,
                                     size_t length, uint32_t timeout_ms)
{
    f407_network_uart_t *port = context;
    status_t status;

    if (port == 0 || data == 0 || length == 0u || length > UINT16_MAX ||
        timeout_ms == 0u) {
        return ERR_INVALID_ARG;
    }
    clear_current_task_notification();
    port->owner_task = xTaskGetCurrentTaskHandle();
    status = status_from_hal(HAL_UART_Transmit_DMA(
        port->handle, (uint8_t *)data, (uint16_t)length));
    if (status == SYS_OK) {
        status = wait_for_uart_event(NETWORK_UART_NOTIFY_TX_DONE,
                                     timeout_ms);
    }
    if (status != SYS_OK) {
        (void)HAL_UART_AbortTransmit(port->handle);
    }
    port->owner_task = 0;
    return status;
}

static status_t network_serial_read(void *context, uint8_t *data,
                                    size_t capacity, size_t *length,
                                    uint32_t timeout_ms)
{
    f407_network_uart_t *port = context;
    status_t status;

    if (port == 0 || data == 0 || capacity == 0u ||
        capacity > UINT16_MAX || length == 0 || timeout_ms == 0u) {
        return ERR_INVALID_ARG;
    }
    *length = 0u;
    clear_current_task_notification();
    port->owner_task = xTaskGetCurrentTaskHandle();
    port->rx_length = 0u;
    status = status_from_hal(HAL_UARTEx_ReceiveToIdle_DMA(
        port->handle, data, (uint16_t)capacity));
    if (status == SYS_OK && port->handle->hdmarx != 0) {
        __HAL_DMA_DISABLE_IT(port->handle->hdmarx, DMA_IT_HT);
        status = wait_for_uart_event(NETWORK_UART_NOTIFY_RX_DONE,
                                     timeout_ms);
    }
    if (status == SYS_OK) {
        *length = port->rx_length;
    } else {
        (void)HAL_UART_AbortReceive(port->handle);
    }
    port->owner_task = 0;
    return status;
}

static status_t network_serial_flush(void *context)
{
    f407_network_uart_t *port = context;

    if (port == 0) {
        return ERR_INVALID_ARG;
    }
    (void)HAL_UART_AbortReceive(port->handle);
    clear_current_task_notification();
    port->rx_length = 0u;
    return SYS_OK;
}

static status_t network_serial_suspend(void *context)
{
    f407_network_uart_t *port = context;

    return port != 0
        ? status_from_hal(HAL_UART_DeInit(port->handle)) : ERR_INVALID_ARG;
}

static status_t network_serial_resume(void *context)
{
    return network_serial_init(context);
}

static void notify_owner_from_isr(f407_network_uart_t *port,
                                  uint32_t event)
{
    BaseType_t should_yield = pdFALSE;

    if (port->owner_task != 0) {
        (void)xTaskNotifyFromISR(port->owner_task, event, eSetBits,
                                &should_yield);
        portYIELD_FROM_ISR(should_yield);
    }
}

void f407_network_uart_tx_complete(UART_HandleTypeDef *handle)
{
    if (handle == network_uart.handle) {
        notify_owner_from_isr(&network_uart, NETWORK_UART_NOTIFY_TX_DONE);
    }
}

void f407_network_uart_rx_event(UART_HandleTypeDef *handle, uint16_t size)
{
    if (handle == network_uart.handle) {
        network_uart.rx_length = size;
        notify_owner_from_isr(&network_uart, NETWORK_UART_NOTIFY_RX_DONE);
    }
}

void f407_network_uart_error(UART_HandleTypeDef *handle)
{
    if (handle == network_uart.handle) {
        notify_owner_from_isr(&network_uart, NETWORK_UART_NOTIFY_ERROR);
    }
}

status_t f407_network_configure(app_context_t *context)
{
    static const esp8266_serial_ops_t serial_ops = {
        network_serial_init,
        network_serial_write,
        network_serial_read,
        network_serial_flush,
        network_serial_suspend,
        network_serial_resume
    };
    app_network_config_t config;

    if (context == 0) {
        return ERR_INVALID_ARG;
    }
    memset(&config, 0, sizeof(config));
    config.serial_ops = &serial_ops;
    config.serial_context = &network_uart;
    config.esp8266.ssid = F407_WIFI_SSID;
    config.esp8266.password = F407_WIFI_PASSWORD;
    config.esp8266.command_timeout_ms = F407_ESP8266_COMMAND_TIMEOUT_MS;
    config.esp8266.join_timeout_ms = F407_ESP8266_JOIN_TIMEOUT_MS;
    config.esp8266.enable_modem_sleep =
        F407_ESP8266_MODEM_SLEEP_ENABLED;
    config.network.device_id = F407_MQTT_DEVICE_ID;
    config.network.client_id = F407_MQTT_CLIENT_ID;
    config.network.username = F407_MQTT_USERNAME;
    config.network.password = F407_MQTT_PASSWORD;
    config.network.broker_host = F407_MQTT_BROKER_HOST;
    config.network.broker_port = F407_MQTT_BROKER_PORT;
    config.network.keep_alive_seconds = F407_MQTT_KEEP_ALIVE_SECONDS;
    config.network.boot_id = HAL_GetUIDw0() ^ HAL_GetUIDw1() ^
                             HAL_GetUIDw2() ^ HAL_GetTick();
    config.network.connect_timeout_ms = F407_NETWORK_CONNECT_TIMEOUT_MS;
    config.network.puback_timeout_ms = F407_MQTT_PUBACK_TIMEOUT_MS;
    config.network.reconnect_initial_ms =
        F407_NETWORK_RECONNECT_INITIAL_MS;
    config.network.reconnect_max_ms = F407_NETWORK_RECONNECT_MAX_MS;
    config.network.publish_retry_limit = F407_MQTT_PUBLISH_RETRY_LIMIT;
    config.ota_manifest_url = F407_OTA_MANIFEST_URL;
    config.ota_http_timeout_ms = F407_ESP8266_COMMAND_TIMEOUT_MS;
    return app_context_configure_network(context, &config);
}
