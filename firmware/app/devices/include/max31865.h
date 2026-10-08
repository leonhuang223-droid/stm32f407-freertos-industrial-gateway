#ifndef GATEWAY_MAX31865_H
#define GATEWAY_MAX31865_H

#include "device.h"
#include "gateway_model.h"
#include "spi_bus.h"

#include <stdint.h>

typedef struct max31865 max31865_t;

typedef struct {
    uint32_t reference_resistor_milliohm;
    uint32_t rtd_nominal_milliohm;
    uint16_t point_id;
    uint16_t bias_settle_ms;
    uint8_t three_wire;
    uint8_t filter_50hz;
} max31865_config_t;

typedef struct {
    status_t (*init)(max31865_t *device);
    status_t (*sample)(max31865_t *device,
                       uint32_t now_ms,
                       gateway_measurement_t *out_measurement);
    status_t (*suspend)(max31865_t *device);
    status_t (*resume)(max31865_t *device);
    status_t (*self_test)(max31865_t *device);
} max31865_ops_t;

struct max31865 {
    const max31865_ops_t *ops;
    spi_device_t *spi;
    max31865_config_t config;
    gateway_device_health_t health;
    uint8_t last_fault_status;
};

status_t max31865_construct(max31865_t *device,
                            spi_device_t *spi,
                            const max31865_config_t *config);
status_t max31865_init(max31865_t *device);
status_t max31865_sample(max31865_t *device,
                         uint32_t now_ms,
                         gateway_measurement_t *out_measurement);
status_t max31865_suspend(max31865_t *device);
status_t max31865_resume(max31865_t *device);
status_t max31865_self_test(max31865_t *device);
status_t max31865_get_health(const max31865_t *device,
                             gateway_device_health_t *out_health);
status_t max31865_temperature_millicelsius(uint16_t rtd_code,
                                           uint32_t reference_resistor_milliohm,
                                           uint32_t rtd_nominal_milliohm,
                                           int32_t *out_temperature);

#endif
