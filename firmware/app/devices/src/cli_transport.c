#include "cli_transport.h"

#include <string.h>

static int transport_valid(const cli_transport_t *transport)
{
    return transport != 0 && transport->ops != 0 && transport->context != 0 &&
           transport->ops->init != 0 && transport->ops->read != 0 &&
           transport->ops->write != 0;
}

status_t cli_transport_construct(cli_transport_t *transport,
                                 const cli_transport_ops_t *ops,
                                 void *context)
{
    if (transport == 0 || ops == 0 || context == 0 || ops->init == 0 ||
        ops->read == 0 || ops->write == 0) {
        return ERR_INVALID_ARG;
    }
    memset(transport, 0, sizeof(*transport));
    transport->ops = ops;
    transport->context = context;
    transport->last_error = ERR_DEVICE_NOT_READY;
    return SYS_OK;
}

status_t cli_transport_init(cli_transport_t *transport)
{
    status_t status;

    if (!transport_valid(transport)) {
        return ERR_INVALID_ARG;
    }
    status = transport->ops->init(transport->context);
    transport->last_error = status;
    transport->initialized = status == SYS_OK ? 1u : 0u;
    transport->suspended = 0u;
    return status;
}

status_t cli_transport_read(cli_transport_t *transport,
                            uint8_t *data,
                            size_t capacity,
                            size_t *length,
                            uint32_t timeout_ms)
{
    status_t status;

    if (length != 0) {
        *length = 0u;
    }
    if (!transport_valid(transport) || data == 0 || capacity == 0u ||
        length == 0) {
        return ERR_INVALID_ARG;
    }
    if (transport->initialized == 0u || transport->suspended != 0u) {
        return ERR_DEVICE_NOT_READY;
    }
    *length = 0u;
    status = transport->ops->read(
        transport->context, data, capacity, length, timeout_ms);
    if (*length > capacity) {
        status = ERR_PROTOCOL;
    }
    if (status != SYS_OK) {
        *length = 0u;
    }
    transport->last_error = status;
    if (status == SYS_OK) {
        transport->rx_bytes += (uint32_t)*length;
    } else if (status != ERR_TIMEOUT) {
        transport->errors++;
    }
    return status;
}

status_t cli_transport_write(cli_transport_t *transport,
                             const uint8_t *data,
                             size_t length,
                             uint32_t timeout_ms)
{
    status_t status;

    if (!transport_valid(transport) || data == 0 || length == 0u) {
        return ERR_INVALID_ARG;
    }
    if (transport->initialized == 0u || transport->suspended != 0u) {
        return ERR_DEVICE_NOT_READY;
    }
    status =
        transport->ops->write(transport->context, data, length, timeout_ms);
    transport->last_error = status;
    if (status == SYS_OK) {
        transport->tx_bytes += (uint32_t)length;
    } else {
        transport->errors++;
    }
    return status;
}

status_t cli_transport_suspend(cli_transport_t *transport)
{
    status_t status;

    if (!transport_valid(transport) || transport->initialized == 0u) {
        return ERR_DEVICE_NOT_READY;
    }
    status = transport->ops->suspend != 0
                 ? transport->ops->suspend(transport->context)
                 : SYS_OK;
    transport->last_error = status;
    if (status == SYS_OK) {
        transport->suspended = 1u;
    }
    return status;
}

status_t cli_transport_resume(cli_transport_t *transport)
{
    status_t status;

    if (!transport_valid(transport)) {
        return ERR_INVALID_ARG;
    }
    status = transport->ops->resume != 0
                 ? transport->ops->resume(transport->context)
                 : transport->ops->init(transport->context);
    transport->last_error = status;
    if (status == SYS_OK) {
        transport->initialized = 1u;
        transport->suspended = 0u;
    }
    return status;
}
