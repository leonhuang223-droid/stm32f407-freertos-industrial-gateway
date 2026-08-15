#include "f407_acquisition_port.h"

#include "f407_board_config.h"
#include "main.h"
#include "spi.h"

#include "FreeRTOS.h"
#include "task.h"

#include <limits.h>

typedef struct {
    I2C_HandleTypeDef *handle;
    uint32_t timeout_ms;
} f407_i2c_port_t;

typedef struct {
    SPI_HandleTypeDef *handle;
    GPIO_TypeDef *cs_port;
    uint16_t cs_pin;
    uint32_t timeout_ms;
} f407_spi_port_t;

static I2C_HandleTypeDef f407_i2c1;

static status_t status_from_hal(HAL_StatusTypeDef status)
{
    switch (status) {
    case HAL_OK: return SYS_OK;
    case HAL_TIMEOUT: return ERR_TIMEOUT;
    case HAL_BUSY: return ERR_DEVICE_NOT_READY;
    default: return ERR_IO;
    }
}

static status_t f407_i2c_write(void *context, uint8_t address,
                               const uint8_t *data, size_t length)
{
    f407_i2c_port_t *port = context;

    if (port == 0 || data == 0 || length == 0u || length > UINT16_MAX) {
        return ERR_INVALID_ARG;
    }
    return status_from_hal(HAL_I2C_Master_Transmit(
        port->handle, (uint16_t)address << 1u, (uint8_t *)data,
        (uint16_t)length, port->timeout_ms));
}

static status_t f407_i2c_read(void *context, uint8_t address,
                              uint8_t *data, size_t length)
{
    f407_i2c_port_t *port = context;

    if (port == 0 || data == 0 || length == 0u || length > UINT16_MAX) {
        return ERR_INVALID_ARG;
    }
    return status_from_hal(HAL_I2C_Master_Receive(
        port->handle, (uint16_t)address << 1u, data, (uint16_t)length,
        port->timeout_ms));
}

static status_t f407_i2c_write_read(void *context, uint8_t address,
                                    const uint8_t *write_data,
                                    size_t write_length,
                                    uint8_t *read_data, size_t read_length)
{
    f407_i2c_port_t *port = context;
    HAL_StatusTypeDef hal_status;

    if (port == 0 || write_data == 0 || read_data == 0 ||
        write_length == 0u || read_length == 0u ||
        write_length > UINT16_MAX || read_length > UINT16_MAX) {
        return ERR_INVALID_ARG;
    }
    if (write_length == 1u) {
        hal_status = HAL_I2C_Mem_Read(
            port->handle, (uint16_t)address << 1u, write_data[0],
            I2C_MEMADD_SIZE_8BIT, read_data, (uint16_t)read_length,
            port->timeout_ms);
    } else {
        hal_status = HAL_I2C_Master_Transmit(
            port->handle, (uint16_t)address << 1u, (uint8_t *)write_data,
            (uint16_t)write_length, port->timeout_ms);
        if (hal_status == HAL_OK) {
            hal_status = HAL_I2C_Master_Receive(
                port->handle, (uint16_t)address << 1u, read_data,
                (uint16_t)read_length, port->timeout_ms);
        }
    }
    return status_from_hal(hal_status);
}

static void f407_delay_ms(void *context, uint32_t delay_ms)
{
    (void)context;
    if (delay_ms == 0u) {
        return;
    }
    if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    } else {
        HAL_Delay(delay_ms);
    }
}

static status_t f407_spi_select(void *context, int active)
{
    f407_spi_port_t *port = context;

    if (port == 0 || (active != 0 && active != 1)) {
        return ERR_INVALID_ARG;
    }
    HAL_GPIO_WritePin(port->cs_port, port->cs_pin,
                      active != 0 ? GPIO_PIN_RESET : GPIO_PIN_SET);
    return SYS_OK;
}

static status_t f407_spi_transfer(void *context, const uint8_t *tx,
                                  uint8_t *rx, size_t length)
{
    f407_spi_port_t *port = context;
    HAL_StatusTypeDef hal_status;

    if (port == 0 || tx == 0 || length == 0u || length > UINT16_MAX) {
        return ERR_INVALID_ARG;
    }
    if (rx != 0) {
        hal_status = HAL_SPI_TransmitReceive(
            port->handle, (uint8_t *)tx, rx, (uint16_t)length,
            port->timeout_ms);
    } else {
        hal_status = HAL_SPI_Transmit(
            port->handle, (uint8_t *)tx, (uint16_t)length,
            port->timeout_ms);
    }
    return status_from_hal(hal_status);
}

static status_t f407_i2c1_init(void)
{
    GPIO_InitTypeDef gpio = { 0 };

    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_I2C1_CLK_ENABLE();
    gpio.Pin = GPIO_PIN_6 | GPIO_PIN_7;
    gpio.Mode = GPIO_MODE_AF_OD;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF4_I2C1;
    HAL_GPIO_Init(GPIOB, &gpio);

    f407_i2c1.Instance = I2C1;
    f407_i2c1.Init.ClockSpeed = 400000u;
    f407_i2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
    f407_i2c1.Init.OwnAddress1 = 0u;
    f407_i2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    f407_i2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    f407_i2c1.Init.OwnAddress2 = 0u;
    f407_i2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    f407_i2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
    return status_from_hal(HAL_I2C_Init(&f407_i2c1));
}

status_t f407_acquisition_configure(app_context_t *context)
{
    static const i2c_bus_ops_t i2c_ops = {
        f407_i2c_write, f407_i2c_read, f407_i2c_write_read, f407_delay_ms
    };
    static const spi_device_ops_t spi_ops = {
        f407_spi_select, f407_spi_transfer, f407_delay_ms
    };
    static f407_i2c_port_t i2c_port = {
        &f407_i2c1, F407_ACQUISITION_I2C_TIMEOUT_MS
    };
    static f407_spi_port_t spi_port = {
        &hspi2, MAX31865_CS_GPIO_Port, MAX31865_CS_Pin,
        F407_MAX31865_SPI_TIMEOUT_MS
    };
    app_acquisition_config_t config = { 0 };

    if (context == 0) {
        return ERR_INVALID_ARG;
    }
    if (f407_i2c1_init() != SYS_OK) {
        return ERR_IO;
    }
    MX_SPI2_Init();
    HAL_GPIO_WritePin(MAX31865_CS_GPIO_Port, MAX31865_CS_Pin, GPIO_PIN_SET);

    config.i2c_ops = &i2c_ops;
    config.i2c_context = &i2c_port;
    config.i2c_timeout_ms = F407_ACQUISITION_I2C_TIMEOUT_MS;
    config.max31865_spi_ops = &spi_ops;
    config.max31865_spi_context = &spi_port;
    config.spi_timeout_ms = F407_MAX31865_SPI_TIMEOUT_MS;

    config.ads1115.address = F407_ADS1115_I2C_ADDRESS;
    config.ads1115.channel = 0u;
    config.ads1115.full_scale = ADS1115_FSR_4096_MV;
    config.ads1115.data_rate = ADS1115_SPS_860;
    config.ads1115.shunt_ohms = F407_LOOP_SHUNT_OHM;
    config.ads1115.valid_min_microamp = F407_LOOP_CURRENT_MIN_UA;
    config.ads1115.valid_max_microamp = F407_LOOP_CURRENT_MAX_UA;
    config.ads1115.point_id = GATEWAY_POINT_LOOP_CURRENT;

    config.max31865.reference_resistor_milliohm =
        F407_MAX31865_REFERENCE_MILLIOHM;
    config.max31865.rtd_nominal_milliohm =
        F407_PT100_NOMINAL_MILLIOHM;
    config.max31865.point_id = GATEWAY_POINT_PT100_TEMPERATURE;
    config.max31865.bias_settle_ms = F407_MAX31865_BIAS_SETTLE_MS;
    config.max31865.three_wire = 1u;
    config.max31865.filter_50hz = 1u;

    config.sht30.address = F407_SHT30_I2C_ADDRESS;
    config.sht30.repeatability = SHT30_REPEATABILITY_HIGH;
    config.sht30.temperature_point_id = GATEWAY_POINT_AMBIENT_TEMPERATURE;
    config.sht30.humidity_point_id = GATEWAY_POINT_RELATIVE_HUMIDITY;

    config.schedule.ads1115_period_ms = F407_ADS1115_SAMPLE_PERIOD_MS;
    config.schedule.max31865_period_ms = F407_MAX31865_SAMPLE_PERIOD_MS;
    config.schedule.sht30_period_ms = F407_SHT30_SAMPLE_PERIOD_MS;
    return app_context_configure_acquisition(context, &config);
}
