#include "ads1115.h"

#include <string.h>

#define ADS1115_REG_CONVERSION 0x00u
#define ADS1115_REG_CONFIG 0x01u
#define ADS1115_CONFIG_OS 0x8000u
#define ADS1115_CONFIG_MODE_SINGLE 0x0100u
#define ADS1115_CONFIG_COMP_DISABLE 0x0003u

static const int32_t full_scale_uv[] = {
    6144000, 4096000, 2048000, 1024000, 512000, 256000
};

static const uint16_t samples_per_second[] = {
    8u, 16u, 32u, 64u, 128u, 250u, 475u, 860u
};

static status_t init_impl(ads1115_t *device);
static status_t sample_impl(ads1115_t *device, uint32_t now_ms,
                            gateway_measurement_t *out_measurement);
static status_t suspend_impl(ads1115_t *device);
static status_t resume_impl(ads1115_t *device);
static status_t self_test_impl(ads1115_t *device);

static const ads1115_ops_t ads1115_ops = {
    init_impl, sample_impl, suspend_impl, resume_impl, self_test_impl
};

static int config_valid(const ads1115_config_t *config)
{
    return config != 0 && config->address >= 0x48u &&
           config->address <= 0x4Bu && config->channel <= 3u &&
           config->full_scale <= ADS1115_FSR_256_MV &&
           config->data_rate <= ADS1115_SPS_860 &&
           config->shunt_ohms != 0u &&
           config->valid_min_microamp < config->valid_max_microamp &&
           config->point_id != 0u;
}

static status_t read_register(ads1115_t *device, uint8_t reg,
                              uint16_t *out_value)
{
    uint8_t data[2];
    status_t status;

    if (out_value == 0) {
        return ERR_INVALID_ARG;
    }
    status = i2c_bus_write_read(device->bus, device->config.address,
                                &reg, 1u, data, sizeof(data));
    if (status == SYS_OK) {
        *out_value = ((uint16_t)data[0] << 8u) | data[1];
    }
    return status;
}

static status_t write_config(ads1115_t *device, uint16_t value)
{
    uint8_t data[3] = {
        ADS1115_REG_CONFIG,
        (uint8_t)(value >> 8u),
        (uint8_t)value
    };

    return i2c_bus_write(device->bus, device->config.address,
                         data, sizeof(data));
}

static void prepare_measurement(const ads1115_t *device, uint32_t now_ms,
                                gateway_measurement_t *measurement)
{
    memset(measurement, 0, sizeof(*measurement));
    measurement->point_id = device->config.point_id;
    measurement->source = GATEWAY_SOURCE_ADS1115;
    measurement->unit = GATEWAY_UNIT_MICROAMP;
    measurement->monotonic_ms = now_ms;
    measurement->quality = GATEWAY_QUALITY_COMM_ERROR;
    measurement->error = ERR_DEVICE_NOT_READY;
}

status_t ads1115_construct(ads1115_t *device, i2c_bus_t *bus,
                           const ads1115_config_t *config)
{
    if (device == 0 || bus == 0 || bus->initialized == 0u ||
        !config_valid(config)) {
        return ERR_INVALID_ARG;
    }
    memset(device, 0, sizeof(*device));
    device->ops = &ads1115_ops;
    device->bus = bus;
    device->config = *config;
    gateway_device_health_reset(&device->health);
    return SYS_OK;
}

static status_t self_test_impl(ads1115_t *device)
{
    uint16_t config;
    return read_register(device, ADS1115_REG_CONFIG, &config);
}

static status_t init_impl(ads1115_t *device)
{
    status_t status = self_test_impl(device);

    if (status == SYS_OK) {
        device->health.initialized = 1u;
        device->health.suspended = 0u;
        device->health.last_error = SYS_OK;
    } else {
        gateway_device_health_record_error(&device->health, status);
    }
    return status;
}

static status_t sample_impl(ads1115_t *device, uint32_t now_ms,
                            gateway_measurement_t *out_measurement)
{
    uint16_t config;
    uint16_t conversion;
    uint16_t rate;
    int16_t raw;
    int32_t microvolts;
    int32_t microamps;
    status_t status;

    prepare_measurement(device, now_ms, out_measurement);
    if (device->health.initialized == 0u || device->health.suspended != 0u) {
        return ERR_DEVICE_NOT_READY;
    }

    config = ADS1115_CONFIG_OS |
             (uint16_t)((uint16_t)(4u + device->config.channel) << 12u) |
             (uint16_t)((uint16_t)device->config.full_scale << 9u) |
             ADS1115_CONFIG_MODE_SINGLE |
             (uint16_t)((uint16_t)device->config.data_rate << 5u) |
             ADS1115_CONFIG_COMP_DISABLE;
    status = write_config(device, config);
    if (status != SYS_OK) {
        goto fail;
    }
    rate = samples_per_second[device->config.data_rate];
    i2c_bus_delay(device->bus, (1000u + rate - 1u) / rate + 1u);
    status = read_register(device, ADS1115_REG_CONFIG, &config);
    if (status != SYS_OK) {
        goto fail;
    }
    if ((config & ADS1115_CONFIG_OS) == 0u) {
        status = ERR_TIMEOUT;
        goto fail;
    }
    status = read_register(device, ADS1115_REG_CONVERSION, &conversion);
    if (status != SYS_OK) {
        goto fail;
    }

    raw = (int16_t)conversion;
    microvolts = (int32_t)(((int64_t)raw *
                            full_scale_uv[device->config.full_scale]) /
                           32768);
    microamps = microvolts / (int32_t)device->config.shunt_ohms;
    out_measurement->raw_value = raw;
    out_measurement->engineering_value = microamps;
    out_measurement->quality =
        microamps < device->config.valid_min_microamp ||
        microamps > device->config.valid_max_microamp
        ? GATEWAY_QUALITY_OUT_OF_RANGE
        : GATEWAY_QUALITY_GOOD;
    out_measurement->error = SYS_OK;
    gateway_device_health_record_success(&device->health, now_ms);
    return SYS_OK;

fail:
    out_measurement->error = status;
    gateway_device_health_record_error(&device->health, status);
    return status;
}

static status_t suspend_impl(ads1115_t *device)
{
    device->health.suspended = 1u;
    return SYS_OK;
}

static status_t resume_impl(ads1115_t *device)
{
    status_t status = self_test_impl(device);
    if (status == SYS_OK) {
        device->health.suspended = 0u;
    }
    return status;
}

status_t ads1115_init(ads1115_t *device)
{
    return device != 0 && device->ops != 0
        ? device->ops->init(device) : ERR_INVALID_ARG;
}

status_t ads1115_sample(ads1115_t *device, uint32_t now_ms,
                        gateway_measurement_t *out_measurement)
{
    return device != 0 && device->ops != 0 && out_measurement != 0
        ? device->ops->sample(device, now_ms, out_measurement)
        : ERR_INVALID_ARG;
}

status_t ads1115_suspend(ads1115_t *device)
{
    return device != 0 && device->ops != 0 && device->health.initialized != 0u
        ? device->ops->suspend(device) : ERR_DEVICE_NOT_READY;
}

status_t ads1115_resume(ads1115_t *device)
{
    return device != 0 && device->ops != 0 && device->health.initialized != 0u
        ? device->ops->resume(device) : ERR_DEVICE_NOT_READY;
}

status_t ads1115_self_test(ads1115_t *device)
{
    return device != 0 && device->ops != 0
        ? device->ops->self_test(device) : ERR_INVALID_ARG;
}

status_t ads1115_get_health(const ads1115_t *device,
                            gateway_device_health_t *out_health)
{
    if (device == 0 || out_health == 0) {
        return ERR_INVALID_ARG;
    }
    *out_health = device->health;
    return SYS_OK;
}
