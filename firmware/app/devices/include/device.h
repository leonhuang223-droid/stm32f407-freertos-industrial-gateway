#ifndef GATEWAY_DEVICE_H
#define GATEWAY_DEVICE_H

#include "error_code.h"

#include <stdint.h>

typedef struct gateway_device gateway_device_t;

typedef struct {
    uint32_t last_success_ms;
    uint32_t total_samples;
    uint32_t total_errors;
    uint32_t consecutive_errors;
    status_t last_error;
    uint8_t initialized;
    uint8_t suspended;
} gateway_device_health_t;

typedef struct {
    status_t (*init)(gateway_device_t *device);
    status_t (*suspend)(gateway_device_t *device);
    status_t (*resume)(gateway_device_t *device);
    status_t (*self_test)(gateway_device_t *device);
} gateway_device_ops_t;

struct gateway_device {
    const gateway_device_ops_t *ops;
    void *context;
    const char *name;
    uint8_t initialized;
};

status_t gateway_device_init(gateway_device_t *device);
status_t gateway_device_suspend(gateway_device_t *device);
status_t gateway_device_resume(gateway_device_t *device);
status_t gateway_device_self_test(gateway_device_t *device);
void gateway_device_health_reset(gateway_device_health_t *health);
void gateway_device_health_record_success(gateway_device_health_t *health,
                                          uint32_t now_ms);
void gateway_device_health_record_error(gateway_device_health_t *health,
                                        status_t error);

#endif
