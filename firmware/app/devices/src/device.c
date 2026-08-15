#include "device.h"

#include <string.h>

static status_t validate_device(const gateway_device_t *device)
{
    return device != 0 && device->ops != 0 && device->name != 0
        ? SYS_OK
        : ERR_INVALID_ARG;
}

status_t gateway_device_init(gateway_device_t *device)
{
    status_t status = validate_device(device);

    if (status != SYS_OK) {
        return status;
    }
    if (device->ops->init == 0) {
        return ERR_UNSUPPORTED;
    }
    status = device->ops->init(device);
    if (status == SYS_OK) {
        device->initialized = 1u;
    }
    return status;
}

status_t gateway_device_suspend(gateway_device_t *device)
{
    if (validate_device(device) != SYS_OK || device->initialized == 0u) {
        return ERR_INVALID_ARG;
    }
    if (device->ops->suspend == 0) {
        return ERR_UNSUPPORTED;
    }
    return device->ops->suspend(device);
}

status_t gateway_device_resume(gateway_device_t *device)
{
    if (validate_device(device) != SYS_OK || device->initialized == 0u) {
        return ERR_INVALID_ARG;
    }
    if (device->ops->resume == 0) {
        return ERR_UNSUPPORTED;
    }
    return device->ops->resume(device);
}

status_t gateway_device_self_test(gateway_device_t *device)
{
    if (validate_device(device) != SYS_OK || device->initialized == 0u) {
        return ERR_INVALID_ARG;
    }
    if (device->ops->self_test == 0) {
        return ERR_UNSUPPORTED;
    }
    return device->ops->self_test(device);
}

void gateway_device_health_reset(gateway_device_health_t *health)
{
    if (health != 0) {
        memset(health, 0, sizeof(*health));
        health->last_error = ERR_DEVICE_NOT_READY;
    }
}

void gateway_device_health_record_success(gateway_device_health_t *health,
                                          uint32_t now_ms)
{
    if (health != 0) {
        health->last_success_ms = now_ms;
        health->total_samples++;
        health->consecutive_errors = 0u;
        health->last_error = SYS_OK;
    }
}

void gateway_device_health_record_error(gateway_device_health_t *health,
                                        status_t error)
{
    if (health != 0) {
        health->total_samples++;
        health->total_errors++;
        health->consecutive_errors++;
        health->last_error = error;
    }
}
