#include "f407_control_storage_port.h"

#include "f407_board_config.h"
#include "main.h"
#include "spi.h"

#include "FreeRTOS.h"
#include "task.h"

#include <limits.h>
#include <string.h>

typedef struct {
    SPI_HandleTypeDef *handle;
} f407_storage_spi_port_t;

typedef struct {
    GPIO_TypeDef *port;
    uint16_t pin;
} f407_relay_port_t;

static f407_storage_spi_port_t storage_spi_port = { &hspi1 };
static f407_relay_port_t relay_port = {
    RELAY_DO_GPIO_Port, RELAY_DO_Pin
};

static status_t status_from_hal(HAL_StatusTypeDef status)
{
    switch (status) {
    case HAL_OK: return SYS_OK;
    case HAL_TIMEOUT: return ERR_TIMEOUT;
    case HAL_BUSY: return ERR_DEVICE_NOT_READY;
    default: return ERR_IO;
    }
}

static status_t storage_spi_select(void *context, int active)
{
    f407_storage_spi_port_t *port = context;

    if (port == 0) {
        return ERR_INVALID_ARG;
    }
    HAL_GPIO_WritePin(W25Q128_CS_GPIO_Port, W25Q128_CS_Pin,
                      active != 0 ? GPIO_PIN_RESET : GPIO_PIN_SET);
    return SYS_OK;
}

static status_t storage_spi_transfer(void *context, const uint8_t *tx,
                                     uint8_t *rx, size_t length)
{
    f407_storage_spi_port_t *port = context;
    HAL_StatusTypeDef status;

    if (port == 0 || tx == 0 || length == 0u || length > UINT16_MAX) {
        return ERR_INVALID_ARG;
    }
    if (rx != 0) {
        status = HAL_SPI_TransmitReceive(
            port->handle, (uint8_t *)(uintptr_t)tx, rx,
            (uint16_t)length, F407_W25Q128_SPI_TIMEOUT_MS);
    } else {
        status = HAL_SPI_Transmit(
            port->handle, (uint8_t *)(uintptr_t)tx,
            (uint16_t)length, F407_W25Q128_SPI_TIMEOUT_MS);
    }
    return status_from_hal(status);
}

static void storage_delay(void *context, uint32_t delay_ms)
{
    (void)context;
    if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    } else {
        HAL_Delay(delay_ms);
    }
}

static status_t relay_port_init(void *context, int inactive_level)
{
    f407_relay_port_t *port = context;
    GPIO_InitTypeDef gpio = { 0 };

    if (port == 0) {
        return ERR_INVALID_ARG;
    }
    __HAL_RCC_GPIOG_CLK_ENABLE();
    HAL_GPIO_WritePin(port->port, port->pin,
                      inactive_level != 0 ? GPIO_PIN_SET : GPIO_PIN_RESET);
    gpio.Pin = port->pin;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_PULLDOWN;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(port->port, &gpio);
    return SYS_OK;
}

static status_t relay_port_write(void *context, int physical_level)
{
    f407_relay_port_t *port = context;

    if (port == 0) {
        return ERR_INVALID_ARG;
    }
    HAL_GPIO_WritePin(port->port, port->pin,
                      physical_level != 0 ? GPIO_PIN_SET : GPIO_PIN_RESET);
    return SYS_OK;
}

static status_t relay_port_read(void *context, int *physical_level)
{
    f407_relay_port_t *port = context;

    if (port == 0 || physical_level == 0) {
        return ERR_INVALID_ARG;
    }
    *physical_level = HAL_GPIO_ReadPin(port->port, port->pin) == GPIO_PIN_SET
        ? 1 : 0;
    return SYS_OK;
}

static status_t relay_port_suspend(void *context)
{
    return context != 0 ? SYS_OK : ERR_INVALID_ARG;
}

static status_t relay_port_resume(void *context)
{
    f407_relay_port_t *port = context;

    return port != 0 ? relay_port_init(port, 0) : ERR_INVALID_ARG;
}

static gateway_runtime_config_t default_runtime_config(void)
{
    gateway_runtime_config_t config;

    memset(&config, 0, sizeof(config));
    config.schema_version = GATEWAY_RUNTIME_CONFIG_SCHEMA_VERSION;
    config.revision = 1u;
    config.relay_safe_energized = 0u;
    config.rule_count = 4u;

    config.rules[0].point_id = GATEWAY_POINT_LOOP_CURRENT;
    config.rules[0].high_enabled = 1u;
    config.rules[0].low_enabled = 1u;
    config.rules[0].relay_on_alarm = 1u;
    config.rules[0].assert_samples = 3u;
    config.rules[0].recover_samples = 3u;
    config.rules[0].high_threshold = 19000;
    config.rules[0].low_threshold = 5000;
    config.rules[0].hysteresis = 500;

    config.rules[1].point_id = GATEWAY_POINT_PT100_TEMPERATURE;
    config.rules[1].high_enabled = 1u;
    config.rules[1].relay_on_alarm = 1u;
    config.rules[1].assert_samples = 3u;
    config.rules[1].recover_samples = 3u;
    config.rules[1].high_threshold = 80000;
    config.rules[1].hysteresis = 3000;

    config.rules[2].point_id = GATEWAY_POINT_AMBIENT_TEMPERATURE;
    config.rules[2].high_enabled = 1u;
    config.rules[2].low_enabled = 1u;
    config.rules[2].relay_on_alarm = 1u;
    config.rules[2].assert_samples = 3u;
    config.rules[2].recover_samples = 3u;
    config.rules[2].high_threshold = 50000;
    config.rules[2].low_threshold = 0;
    config.rules[2].hysteresis = 2000;

    config.rules[3].point_id = GATEWAY_POINT_RELATIVE_HUMIDITY;
    config.rules[3].high_enabled = 1u;
    config.rules[3].low_enabled = 1u;
    config.rules[3].relay_on_alarm = 1u;
    config.rules[3].assert_samples = 3u;
    config.rules[3].recover_samples = 3u;
    config.rules[3].high_threshold = 85000;
    config.rules[3].low_threshold = 10000;
    config.rules[3].hysteresis = 5000;
    return config;
}

status_t f407_control_storage_configure(app_context_t *context)
{
    static const spi_device_ops_t spi_ops = {
        storage_spi_select, storage_spi_transfer, storage_delay
    };
    static const relay_ops_t relay_ops = {
        relay_port_init, relay_port_write, relay_port_read,
        relay_port_suspend, relay_port_resume
    };
    app_control_storage_config_t config;

    if (context == 0) {
        return ERR_INVALID_ARG;
    }
    memset(&config, 0, sizeof(config));
    config.storage_spi_ops = &spi_ops;
    config.storage_spi_context = &storage_spi_port;
    config.storage_spi_timeout_ms = F407_W25Q128_SPI_TIMEOUT_MS;
    config.w25q128.expected_jedec_id = EXTERNAL_FLASH_JEDEC_ID;
    config.w25q128.total_size = EXTERNAL_FLASH_TOTAL_SIZE;
    config.w25q128.operation_timeout_ms = F407_W25Q128_OPERATION_TIMEOUT_MS;
    config.relay_ops = &relay_ops;
    config.relay_context = &relay_port;
    config.relay.active_high = 1u;
    config.relay.safe_state = RELAY_DEENERGIZED;
    config.default_runtime_config = default_runtime_config();
    return app_context_configure_control_storage(context, &config);
}
