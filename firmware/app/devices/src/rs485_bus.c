#include "rs485_bus.h"

#include <string.h>

status_t rs485_bus_construct(rs485_bus_t *bus,
                             const rs485_bus_ops_t *ops,
                             void *context,
                             uint32_t timeout_ms)
{
    if (bus == 0 || ops == 0 || ops->exchange == 0 || context == 0 ||
        timeout_ms == 0u) {
        return ERR_INVALID_ARG;
    }
    memset(bus, 0, sizeof(*bus));
    bus->ops = ops;
    bus->context = context;
    bus->timeout_ms = timeout_ms;
    return SYS_OK;
}

status_t rs485_bus_exchange(rs485_bus_t *bus,
                            const rs485_transfer_t *parameters)
{
    if (parameters == 0) {
        return ERR_INVALID_ARG;
    }
    const uint8_t *request = parameters->request;
    size_t request_length = parameters->request_length;
    uint8_t *response = parameters->response;
    size_t response_capacity = parameters->response_capacity;
    size_t *response_length = parameters->response_length;

    status_t status;
    if (response_length != 0) {
        *response_length = 0u;
    }
    if (bus == 0 || bus->ops == 0 || bus->ops->exchange == 0 || request == 0 ||
        request_length == 0u || response == 0 || response_capacity == 0u ||
        response_length == 0) {
        return ERR_INVALID_ARG;
    }
    *response_length = 0u;
    if (bus->suspended != 0u) {
        return ERR_DEVICE_NOT_READY;
    }
    status = bus->ops->exchange(bus->context, parameters, bus->timeout_ms);
    if (*response_length > response_capacity) {
        status = ERR_PROTOCOL;
    }
    if (status != SYS_OK) {
        *response_length = 0u;
    }
    return status;
}

status_t rs485_bus_suspend(rs485_bus_t *bus)
{
    status_t status;

    if (bus == 0 || bus->ops == 0) {
        return ERR_INVALID_ARG;
    }
    if (bus->suspended != 0u) {
        return SYS_OK;
    }
    status = bus->ops->suspend != 0 ? bus->ops->suspend(bus->context) : SYS_OK;
    if (status == SYS_OK) {
        bus->suspended = 1u;
    }
    return status;
}

status_t rs485_bus_resume(rs485_bus_t *bus)
{
    status_t status;

    if (bus == 0 || bus->ops == 0) {
        return ERR_INVALID_ARG;
    }
    if (bus->suspended == 0u) {
        return SYS_OK;
    }
    status = bus->ops->resume != 0 ? bus->ops->resume(bus->context) : SYS_OK;
    if (status == SYS_OK) {
        bus->suspended = 0u;
    }
    return status;
}
