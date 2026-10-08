#ifndef GATEWAY_ADS1115_H
#define GATEWAY_ADS1115_H

#include "device.h"
#include "gateway_model.h"
#include "i2c_bus.h"

#include <stdint.h>

typedef struct ads1115 ads1115_t;

typedef enum {
    ADS1115_FSR_6144_MV = 0,
    ADS1115_FSR_4096_MV,
    ADS1115_FSR_2048_MV,
    ADS1115_FSR_1024_MV,
    ADS1115_FSR_512_MV,
    ADS1115_FSR_256_MV
} ads1115_fsr_t;

typedef enum {
    ADS1115_SPS_8 = 0,
    ADS1115_SPS_16,
    ADS1115_SPS_32,
    ADS1115_SPS_64,
    ADS1115_SPS_128,
    ADS1115_SPS_250,
    ADS1115_SPS_475,
    ADS1115_SPS_860
} ads1115_data_rate_t;

typedef struct {
    uint8_t address;
    uint8_t channel;
    ads1115_fsr_t full_scale;
    ads1115_data_rate_t data_rate;
    uint16_t shunt_ohms;
    int32_t valid_min_microamp;
    int32_t valid_max_microamp;
    uint16_t point_id;
} ads1115_config_t;

typedef struct {
    status_t (*init)(ads1115_t *device);
    status_t (*sample)(ads1115_t *device,
                       uint32_t now_ms,
                       gateway_measurement_t *out_measurement);
    status_t (*suspend)(ads1115_t *device);
    status_t (*resume)(ads1115_t *device);
    status_t (*self_test)(ads1115_t *device);
} ads1115_ops_t;

struct ads1115 {
    const ads1115_ops_t *ops;
    i2c_bus_t *bus;
    ads1115_config_t config;
    gateway_device_health_t health;
};

status_t ads1115_construct(ads1115_t *device,
                           i2c_bus_t *bus,
                           const ads1115_config_t *config);
status_t ads1115_init(ads1115_t *device);
status_t ads1115_sample(ads1115_t *device,
                        uint32_t now_ms,
                        gateway_measurement_t *out_measurement);
status_t ads1115_suspend(ads1115_t *device);
status_t ads1115_resume(ads1115_t *device);
status_t ads1115_self_test(ads1115_t *device);
status_t ads1115_get_health(const ads1115_t *device,
                            gateway_device_health_t *out_health);

#endif
