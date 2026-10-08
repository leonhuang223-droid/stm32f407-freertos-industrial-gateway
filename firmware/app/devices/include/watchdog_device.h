#ifndef GATEWAY_WATCHDOG_DEVICE_H
#define GATEWAY_WATCHDOG_DEVICE_H

#include "error_code.h"

#include <stdint.h>

typedef struct {
    status_t (*start)(void *context, uint32_t timeout_ms);
    status_t (*refresh)(void *context);
    uint32_t (*remaining_ms)(const void *context);
} watchdog_device_ops_t;

typedef struct {
    const watchdog_device_ops_t *ops;
    void *context;
    uint32_t timeout_ms;
    uint32_t refresh_count;
    uint32_t failure_count;
    status_t last_error;
    uint8_t started;
} watchdog_device_t;

status_t watchdog_device_construct(watchdog_device_t *device,
                                   const watchdog_device_ops_t *ops,
                                   void *context);
status_t watchdog_device_start(watchdog_device_t *device, uint32_t timeout_ms);
status_t watchdog_device_refresh(watchdog_device_t *device);
uint32_t watchdog_device_remaining_ms(const watchdog_device_t *device);

#endif
