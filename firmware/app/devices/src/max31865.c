#include "max31865.h"

#include <math.h>
#include <string.h>

#define MAX31865_REG_CONFIG 0x00u
#define MAX31865_REG_RTD_MSB 0x01u
#define MAX31865_REG_FAULT_STATUS 0x07u

#define MAX31865_CONFIG_BIAS 0x80u
#define MAX31865_CONFIG_ONE_SHOT 0x20u
#define MAX31865_CONFIG_THREE_WIRE 0x10u
#define MAX31865_CONFIG_FAULT_CLEAR 0x02u
#define MAX31865_CONFIG_FILTER_50HZ 0x01u

static status_t init_impl(max31865_t *device);
static status_t sample_impl(max31865_t *device,
                            uint32_t now_ms,
                            gateway_measurement_t *out_measurement);
static status_t suspend_impl(max31865_t *device);
static status_t resume_impl(max31865_t *device);
static status_t self_test_impl(max31865_t *device);

static const max31865_ops_t max31865_ops = {
    init_impl, sample_impl, suspend_impl, resume_impl, self_test_impl};

static uint8_t base_config(const max31865_t *device)
{
    return (device->config.three_wire != 0u ? MAX31865_CONFIG_THREE_WIRE : 0u) |
           (device->config.filter_50hz != 0u ? MAX31865_CONFIG_FILTER_50HZ
                                             : 0u);
}

static status_t write_register(max31865_t *device, uint8_t reg, uint8_t value)
{
    uint8_t tx[2] = {(uint8_t)(reg | 0x80u), value};
    return spi_device_transfer(device->spi, tx, 0, sizeof(tx));
}

static status_t
read_registers(max31865_t *device, uint8_t reg, uint8_t *data, size_t length)
{
    uint8_t tx[9];
    uint8_t rx[9];
    status_t status;

    if (data == 0 || length == 0u || length > 8u) {
        return ERR_INVALID_ARG;
    }
    memset(tx, 0xFF, length + 1u);
    memset(rx, 0, length + 1u);
    tx[0] = (uint8_t)(reg & 0x7Fu);
    status = spi_device_transfer(device->spi, tx, rx, length + 1u);
    if (status == SYS_OK) {
        memcpy(data, &rx[1], length);
    }
    return status;
}

status_t max31865_temperature_millicelsius(uint16_t rtd_code,
                                           uint32_t reference_resistor_milliohm,
                                           uint32_t rtd_nominal_milliohm,
                                           int32_t *out_temperature)
{
    const float coefficient_a = 3.90830e-3f;
    const float coefficient_b = -5.77500e-7f;
    const float coefficient_c = -4.18301e-12f;
    float resistance;
    float ratio;
    float temperature;
    float discriminant;
    unsigned int iteration;

    if (rtd_code == 0u || reference_resistor_milliohm == 0u ||
        rtd_nominal_milliohm == 0u || out_temperature == 0) {
        return ERR_INVALID_ARG;
    }
    resistance =
        ((float)rtd_code * (float)reference_resistor_milliohm) / 32768.0f;
    ratio = resistance / (float)rtd_nominal_milliohm;

    if (ratio >= 1.0f) {
        discriminant = coefficient_a * coefficient_a -
                       4.0f * coefficient_b * (1.0f - ratio);
        if (discriminant < 0.0f) {
            return ERR_SENSOR_FAULT;
        }
        temperature =
            (-coefficient_a + sqrtf(discriminant)) / (2.0f * coefficient_b);
    } else {
        temperature = (ratio - 1.0f) / coefficient_a;
        for (iteration = 0u; iteration < 6u; ++iteration) {
            float t2 = temperature * temperature;
            float t3 = t2 * temperature;
            float function =
                1.0f + coefficient_a * temperature + coefficient_b * t2 +
                coefficient_c * (temperature - 100.0f) * t3 - ratio;
            float derivative = coefficient_a +
                               2.0f * coefficient_b * temperature +
                               coefficient_c * (4.0f * t3 - 300.0f * t2);
            if (derivative == 0.0f) {
                return ERR_SENSOR_FAULT;
            }
            temperature -= function / derivative;
        }
    }
    if (temperature < -200.0f || temperature > 850.0f) {
        return ERR_SENSOR_FAULT;
    }
    *out_temperature =
        (int32_t)(temperature * 1000.0f + (temperature >= 0.0f ? 0.5f : -0.5f));
    return SYS_OK;
}

static void prepare_measurement(const max31865_t *device,
                                uint32_t now_ms,
                                gateway_measurement_t *measurement)
{
    memset(measurement, 0, sizeof(*measurement));
    measurement->point_id = device->config.point_id;
    measurement->source = GATEWAY_SOURCE_MAX31865;
    measurement->unit = GATEWAY_UNIT_MILLICELSIUS;
    measurement->monotonic_ms = now_ms;
    measurement->quality = GATEWAY_QUALITY_COMM_ERROR;
    measurement->error = ERR_DEVICE_NOT_READY;
}

status_t max31865_construct(max31865_t *device,
                            spi_device_t *spi,
                            const max31865_config_t *config)
{
    if (device == 0 || spi == 0 || spi->initialized == 0u || config == 0 ||
        config->reference_resistor_milliohm < 100000u ||
        config->rtd_nominal_milliohm == 0u || config->point_id == 0u ||
        config->bias_settle_ms < 1u) {
        return ERR_INVALID_ARG;
    }
    memset(device, 0, sizeof(*device));
    device->ops = &max31865_ops;
    device->spi = spi;
    device->config = *config;
    gateway_device_health_reset(&device->health);
    return SYS_OK;
}

static status_t self_test_impl(max31865_t *device)
{
    uint8_t config;
    status_t status = read_registers(device, MAX31865_REG_CONFIG, &config, 1u);

    if (status == SYS_OK &&
        (config & (MAX31865_CONFIG_THREE_WIRE | MAX31865_CONFIG_FILTER_50HZ)) !=
            base_config(device)) {
        status = ERR_SENSOR_FAULT;
    }
    return status;
}

static status_t init_impl(max31865_t *device)
{
    status_t status = write_register(
        device,
        MAX31865_REG_CONFIG,
        (uint8_t)(base_config(device) | MAX31865_CONFIG_FAULT_CLEAR));

    if (status == SYS_OK) {
        status = self_test_impl(device);
    }
    if (status == SYS_OK) {
        device->health.initialized = 1u;
        device->health.suspended = 0u;
        device->health.last_error = SYS_OK;
    } else {
        gateway_device_health_record_error(&device->health, status);
    }
    return status;
}

static status_t sample_impl(max31865_t *device,
                            uint32_t now_ms,
                            gateway_measurement_t *out_measurement)
{
    uint8_t bytes[2];
    uint8_t fault = 0u;
    uint16_t register_value;
    uint16_t rtd_code;
    uint8_t config = base_config(device);
    status_t status;
    status_t shutdown_status;

    prepare_measurement(device, now_ms, out_measurement);
    if (device->health.initialized == 0u || device->health.suspended != 0u) {
        return ERR_DEVICE_NOT_READY;
    }
    status = write_register(
        device, MAX31865_REG_CONFIG, (uint8_t)(config | MAX31865_CONFIG_BIAS));
    if (status == SYS_OK) {
        spi_device_delay(device->spi, device->config.bias_settle_ms);
        status = write_register(device,
                                MAX31865_REG_CONFIG,
                                (uint8_t)(config | MAX31865_CONFIG_BIAS |
                                          MAX31865_CONFIG_ONE_SHOT));
    }
    if (status == SYS_OK) {
        spi_device_delay(device->spi,
                         device->config.filter_50hz != 0u ? 66u : 55u);
        status =
            read_registers(device, MAX31865_REG_RTD_MSB, bytes, sizeof(bytes));
    }
    shutdown_status = write_register(device, MAX31865_REG_CONFIG, config);
    if (status == SYS_OK) {
        status = shutdown_status;
    }
    if (status != SYS_OK) {
        out_measurement->error = status;
        gateway_device_health_record_error(&device->health, status);
        return status;
    }

    register_value = ((uint16_t)bytes[0] << 8u) | bytes[1];
    rtd_code = register_value >> 1u;
    out_measurement->raw_value = rtd_code;
    if ((register_value & 1u) != 0u) {
        (void)read_registers(device, MAX31865_REG_FAULT_STATUS, &fault, 1u);
        device->last_fault_status = fault;
        (void)write_register(device,
                             MAX31865_REG_CONFIG,
                             (uint8_t)(config | MAX31865_CONFIG_FAULT_CLEAR));
        out_measurement->quality = GATEWAY_QUALITY_SENSOR_FAULT;
        out_measurement->error = ERR_SENSOR_FAULT;
        gateway_device_health_record_error(&device->health, ERR_SENSOR_FAULT);
        return ERR_SENSOR_FAULT;
    }

    status = max31865_temperature_millicelsius(
        rtd_code,
        device->config.reference_resistor_milliohm,
        device->config.rtd_nominal_milliohm,
        &out_measurement->engineering_value);
    if (status != SYS_OK) {
        out_measurement->quality = GATEWAY_QUALITY_SENSOR_FAULT;
        out_measurement->error = status;
        gateway_device_health_record_error(&device->health, status);
        return status;
    }
    device->last_fault_status = 0u;
    out_measurement->quality = GATEWAY_QUALITY_GOOD;
    out_measurement->error = SYS_OK;
    gateway_device_health_record_success(&device->health, now_ms);
    return SYS_OK;
}

static status_t suspend_impl(max31865_t *device)
{
    status_t status =
        write_register(device, MAX31865_REG_CONFIG, base_config(device));
    if (status == SYS_OK) {
        device->health.suspended = 1u;
    }
    return status;
}

static status_t resume_impl(max31865_t *device)
{
    status_t status = self_test_impl(device);
    if (status == SYS_OK) {
        device->health.suspended = 0u;
    }
    return status;
}

status_t max31865_init(max31865_t *device)
{
    return device != 0 && device->ops != 0 ? device->ops->init(device)
                                           : ERR_INVALID_ARG;
}

status_t max31865_sample(max31865_t *device,
                         uint32_t now_ms,
                         gateway_measurement_t *out_measurement)
{
    return device != 0 && device->ops != 0 && out_measurement != 0
               ? device->ops->sample(device, now_ms, out_measurement)
               : ERR_INVALID_ARG;
}

status_t max31865_suspend(max31865_t *device)
{
    return device != 0 && device->ops != 0 && device->health.initialized != 0u
               ? device->ops->suspend(device)
               : ERR_DEVICE_NOT_READY;
}

status_t max31865_resume(max31865_t *device)
{
    return device != 0 && device->ops != 0 && device->health.initialized != 0u
               ? device->ops->resume(device)
               : ERR_DEVICE_NOT_READY;
}

status_t max31865_self_test(max31865_t *device)
{
    return device != 0 && device->ops != 0 ? device->ops->self_test(device)
                                           : ERR_INVALID_ARG;
}

status_t max31865_get_health(const max31865_t *device,
                             gateway_device_health_t *out_health)
{
    if (device == 0 || out_health == 0) {
        return ERR_INVALID_ARG;
    }
    *out_health = device->health;
    return SYS_OK;
}
