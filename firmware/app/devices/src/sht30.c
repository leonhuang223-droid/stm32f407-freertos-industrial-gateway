#include "sht30.h"

#include <string.h>

static const uint16_t measurement_commands[] = {0x2416u, 0x240Bu, 0x2400u};

static const uint8_t measurement_delays_ms[] = {5u, 7u, 16u};

static status_t init_impl(sht30_t *device);
static status_t sample_impl(sht30_t *device,
                            uint32_t now_ms,
                            gateway_measurement_t *out_measurements,
                            size_t capacity,
                            size_t *out_count);
static status_t suspend_impl(sht30_t *device);
static status_t resume_impl(sht30_t *device);
static status_t self_test_impl(sht30_t *device);

static const sht30_ops_t sht30_ops = {
    init_impl, sample_impl, suspend_impl, resume_impl, self_test_impl};

uint8_t sht30_crc8(const uint8_t *data, size_t length)
{
    uint8_t crc = 0xFFu;
    size_t i;
    uint8_t bit;

    if (data == 0) {
        return 0u;
    }
    for (i = 0u; i < length; ++i) {
        crc ^= data[i];
        for (bit = 0u; bit < 8u; ++bit) {
            crc = (crc & 0x80u) != 0u ? (uint8_t)((crc << 1u) ^ 0x31u)
                                      : (uint8_t)(crc << 1u);
        }
    }
    return crc;
}

static status_t write_command(sht30_t *device, uint16_t command)
{
    uint8_t bytes[2] = {(uint8_t)(command >> 8u), (uint8_t)command};
    return i2c_bus_write(
        device->bus, device->config.address, bytes, sizeof(bytes));
}

static void prepare_measurements(const sht30_t *device,
                                 uint32_t now_ms,
                                 gateway_measurement_t *measurements)
{
    memset(measurements, 0, 2u * sizeof(*measurements));
    measurements[0].point_id = device->config.temperature_point_id;
    measurements[0].source = GATEWAY_SOURCE_SHT30;
    measurements[0].unit = GATEWAY_UNIT_MILLICELSIUS;
    measurements[1].point_id = device->config.humidity_point_id;
    measurements[1].source = GATEWAY_SOURCE_SHT30;
    measurements[1].unit = GATEWAY_UNIT_MILLIPERCENT_RH;
    measurements[0].monotonic_ms = now_ms;
    measurements[1].monotonic_ms = now_ms;
    measurements[0].quality = GATEWAY_QUALITY_COMM_ERROR;
    measurements[1].quality = GATEWAY_QUALITY_COMM_ERROR;
    measurements[0].error = ERR_DEVICE_NOT_READY;
    measurements[1].error = ERR_DEVICE_NOT_READY;
}

status_t
sht30_construct(sht30_t *device, i2c_bus_t *bus, const sht30_config_t *config)
{
    if (device == 0 || bus == 0 || bus->initialized == 0u || config == 0 ||
        (config->address != 0x44u && config->address != 0x45u) ||
        config->repeatability > SHT30_REPEATABILITY_HIGH ||
        config->temperature_point_id == 0u || config->humidity_point_id == 0u) {
        return ERR_INVALID_ARG;
    }
    memset(device, 0, sizeof(*device));
    device->ops = &sht30_ops;
    device->bus = bus;
    device->config = *config;
    gateway_device_health_reset(&device->health);
    return SYS_OK;
}

static status_t self_test_impl(sht30_t *device)
{
    uint8_t command[2] = {0xF3u, 0x2Du};
    uint8_t status_bytes[3];
    status_t status = i2c_bus_write_read(
        device->bus,
        device->config.address,
        &(const i2c_transfer_t){
            command, sizeof(command), status_bytes, sizeof(status_bytes)});

    if (status == SYS_OK && sht30_crc8(status_bytes, 2u) != status_bytes[2]) {
        status = ERR_CRC;
    }
    return status;
}

static status_t init_impl(sht30_t *device)
{
    status_t status = write_command(device, 0x30A2u);

    if (status == SYS_OK) {
        i2c_bus_delay(device->bus, 2u);
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

static status_t sample_impl(sht30_t *device,
                            uint32_t now_ms,
                            gateway_measurement_t *out_measurements,
                            size_t capacity,
                            size_t *out_count)
{
    uint8_t data[6];
    uint16_t raw_temperature;
    uint16_t raw_humidity;
    status_t status;

    if (capacity < 2u || out_count == 0) {
        return ERR_INVALID_ARG;
    }
    *out_count = 2u;
    prepare_measurements(device, now_ms, out_measurements);
    if (device->health.initialized == 0u || device->health.suspended != 0u) {
        return ERR_DEVICE_NOT_READY;
    }
    status = write_command(device,
                           measurement_commands[device->config.repeatability]);
    if (status == SYS_OK) {
        i2c_bus_delay(device->bus,
                      measurement_delays_ms[device->config.repeatability]);
        status = i2c_bus_read(
            device->bus, device->config.address, data, sizeof(data));
    }
    if (status == SYS_OK && (sht30_crc8(data, 2u) != data[2] ||
                             sht30_crc8(&data[3], 2u) != data[5])) {
        status = ERR_CRC;
    }
    if (status != SYS_OK) {
        out_measurements[0].error = status;
        out_measurements[1].error = status;
        gateway_device_health_record_error(&device->health, status);
        return status;
    }

    raw_temperature = ((uint16_t)data[0] << 8u) | data[1];
    raw_humidity = ((uint16_t)data[3] << 8u) | data[4];
    out_measurements[0].raw_value = raw_temperature;
    out_measurements[0].engineering_value =
        -45000 + (int32_t)(((int64_t)175000 * raw_temperature) / 65535);
    out_measurements[1].raw_value = raw_humidity;
    out_measurements[1].engineering_value =
        (int32_t)(((int64_t)100000 * raw_humidity) / 65535);
    out_measurements[0].quality = GATEWAY_QUALITY_GOOD;
    out_measurements[1].quality = GATEWAY_QUALITY_GOOD;
    out_measurements[0].error = SYS_OK;
    out_measurements[1].error = SYS_OK;
    gateway_device_health_record_success(&device->health, now_ms);
    return SYS_OK;
}

static status_t suspend_impl(sht30_t *device)
{
    device->health.suspended = 1u;
    return SYS_OK;
}

static status_t resume_impl(sht30_t *device)
{
    status_t status = self_test_impl(device);
    if (status == SYS_OK) {
        device->health.suspended = 0u;
    }
    return status;
}

status_t sht30_init(sht30_t *device)
{
    return device != 0 && device->ops != 0 ? device->ops->init(device)
                                           : ERR_INVALID_ARG;
}

status_t sht30_sample(sht30_t *device,
                      uint32_t now_ms,
                      gateway_measurement_t *out_measurements,
                      size_t capacity,
                      size_t *out_count)
{
    return device != 0 && device->ops != 0 && out_measurements != 0
               ? device->ops->sample(
                     device, now_ms, out_measurements, capacity, out_count)
               : ERR_INVALID_ARG;
}

status_t sht30_suspend(sht30_t *device)
{
    return device != 0 && device->ops != 0 && device->health.initialized != 0u
               ? device->ops->suspend(device)
               : ERR_DEVICE_NOT_READY;
}

status_t sht30_resume(sht30_t *device)
{
    return device != 0 && device->ops != 0 && device->health.initialized != 0u
               ? device->ops->resume(device)
               : ERR_DEVICE_NOT_READY;
}

status_t sht30_self_test(sht30_t *device)
{
    return device != 0 && device->ops != 0 ? device->ops->self_test(device)
                                           : ERR_INVALID_ARG;
}

status_t sht30_get_health(const sht30_t *device,
                          gateway_device_health_t *out_health)
{
    if (device == 0 || out_health == 0) {
        return ERR_INVALID_ARG;
    }
    *out_health = device->health;
    return SYS_OK;
}
