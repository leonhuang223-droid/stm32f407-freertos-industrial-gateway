#include "watchdog_device.h"

#include <string.h>

static int ops_complete(const watchdog_device_ops_t *ops)
{
    return ops != 0 && ops->start != 0 && ops->refresh != 0 &&
           ops->remaining_ms != 0;
}

status_t watchdog_device_construct(watchdog_device_t *device,
                                   const watchdog_device_ops_t *ops,
                                   void *context)
{
    if (device == 0 || !ops_complete(ops)) {
        return ERR_INVALID_ARG;
    }
    memset(device, 0, sizeof(*device));
    device->ops = ops;
    device->context = context;
    device->last_error = SYS_OK;
    return SYS_OK;
}

status_t watchdog_device_start(watchdog_device_t *device, uint32_t timeout_ms)
{
    status_t status;

    if (device == 0 || device->ops == 0 || timeout_ms == 0u ||
        device->started != 0u) {
        return ERR_INVALID_ARG;
    }
    status = device->ops->start(device->context, timeout_ms);
    device->last_error = status;
    if (status == SYS_OK) {
        device->timeout_ms = timeout_ms;
        device->started = 1u;
    } else {
        device->failure_count++;
    }
    return status;
}

status_t watchdog_device_refresh(watchdog_device_t *device)
{
    status_t status;

    if (device == 0 || device->ops == 0 || device->started == 0u) {
        return ERR_DEVICE_NOT_READY;
    }
    status = device->ops->refresh(device->context);
    device->last_error = status;
    if (status == SYS_OK) {
        device->refresh_count++;
    } else {
        device->failure_count++;
    }
    return status;
}

uint32_t watchdog_device_remaining_ms(const watchdog_device_t *device)
{
    if (device == 0 || device->ops == 0 || device->started == 0u) {
        return 0u;
    }
    return device->ops->remaining_ms(device->context);
}
