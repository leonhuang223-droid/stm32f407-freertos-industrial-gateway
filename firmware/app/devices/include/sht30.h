#ifndef GATEWAY_SHT30_H
#define GATEWAY_SHT30_H

#include "device.h"
#include "gateway_model.h"
#include "i2c_bus.h"

#include <stddef.h>
#include <stdint.h>

typedef struct sht30 sht30_t;

typedef enum {
    SHT30_REPEATABILITY_LOW = 0,
    SHT30_REPEATABILITY_MEDIUM,
    SHT30_REPEATABILITY_HIGH
} sht30_repeatability_t;

typedef struct {
    uint8_t address;
    sht30_repeatability_t repeatability;
    uint16_t temperature_point_id;
    uint16_t humidity_point_id;
} sht30_config_t;

typedef struct {
    status_t (*init)(sht30_t *device);
    status_t (*sample)(sht30_t *device,
                       uint32_t now_ms,
                       gateway_measurement_t *out_measurements,
                       size_t capacity,
                       size_t *out_count);
    status_t (*suspend)(sht30_t *device);
    status_t (*resume)(sht30_t *device);
    status_t (*self_test)(sht30_t *device);
} sht30_ops_t;

struct sht30 {
    const sht30_ops_t *ops;
    i2c_bus_t *bus;
    sht30_config_t config;
    gateway_device_health_t health;
};

status_t
sht30_construct(sht30_t *device, i2c_bus_t *bus, const sht30_config_t *config);
status_t sht30_init(sht30_t *device);
status_t sht30_sample(sht30_t *device,
                      uint32_t now_ms,
                      gateway_measurement_t *out_measurements,
                      size_t capacity,
                      size_t *out_count);
status_t sht30_suspend(sht30_t *device);
status_t sht30_resume(sht30_t *device);
status_t sht30_self_test(sht30_t *device);
status_t sht30_get_health(const sht30_t *device,
                          gateway_device_health_t *out_health);
uint8_t sht30_crc8(const uint8_t *data, size_t length);

#endif
