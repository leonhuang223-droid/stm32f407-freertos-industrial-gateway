#include "spi_bus.h"

#include <string.h>

status_t spi_device_construct(spi_device_t *device,
                              const spi_device_ops_t *ops,
                              void *context, uint32_t timeout_ms)
{
    if (device == 0 || ops == 0 || ops->select == 0 ||
        ops->transfer == 0 || ops->delay_ms == 0 || timeout_ms == 0u) {
        return ERR_INVALID_ARG;
    }
    memset(device, 0, sizeof(*device));
    device->ops = ops;
    device->context = context;
    device->timeout_ms = timeout_ms;
    device->initialized = 1u;
    return SYS_OK;
}

status_t spi_device_transfer(spi_device_t *device, const uint8_t *tx,
                             uint8_t *rx, size_t length)
{
    status_t status;
    status_t deselect_status;

    if (device == 0 || device->initialized == 0u || device->ops == 0) {
        return ERR_DEVICE_NOT_READY;
    }
    if (tx == 0 || length == 0u) {
        return ERR_INVALID_ARG;
    }
    status = device->ops->select(device->context, 1);
    if (status == SYS_OK) {
        status = device->ops->transfer(device->context, tx, rx, length);
    }
    deselect_status = device->ops->select(device->context, 0);
    return status == SYS_OK ? deselect_status : status;
}

void spi_device_delay(spi_device_t *device, uint32_t delay_ms)
{
    if (device != 0 && device->initialized != 0u && device->ops != 0 &&
        delay_ms != 0u) {
        device->ops->delay_ms(device->context, delay_ms);
    }
}
