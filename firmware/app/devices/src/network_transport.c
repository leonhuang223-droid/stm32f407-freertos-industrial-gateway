#include "network_transport.h"

#include <string.h>

status_t network_transport_construct(network_transport_t *transport,
                                     const network_transport_ops_t *ops,
                                     void *context)
{
    if (transport == 0 || ops == 0 || context == 0 || ops->init == 0 ||
        ops->connect == 0 || ops->send == 0 || ops->receive == 0 ||
        ops->close == 0) {
        return ERR_INVALID_ARG;
    }
    memset(transport, 0, sizeof(*transport));
    transport->ops = ops;
    transport->context = context;
    return SYS_OK;
}

status_t network_transport_init(network_transport_t *transport)
{
    status_t status;

    if (transport == 0 || transport->ops == 0) {
        return ERR_INVALID_ARG;
    }
    status = transport->ops->init(transport->context);
    transport->initialized = status == SYS_OK ? 1u : 0u;
    transport->connected = 0u;
    return status;
}

status_t network_transport_connect(network_transport_t *transport,
                                    const char *host, uint16_t port)
{
    status_t status;

    if (transport == 0 || host == 0 || host[0] == '\0' || port == 0u ||
        transport->initialized == 0u) {
        return ERR_DEVICE_NOT_READY;
    }
    status = transport->ops->connect(transport->context, host, port);
    transport->connected = status == SYS_OK ? 1u : 0u;
    return status;
}

status_t network_transport_send(network_transport_t *transport,
                                 const uint8_t *data, size_t length)
{
    if (transport == 0 || data == 0 || length == 0u ||
        transport->connected == 0u) {
        return ERR_DEVICE_NOT_READY;
    }
    return transport->ops->send(transport->context, data, length);
}

status_t network_transport_receive(network_transport_t *transport,
                                    uint8_t *data, size_t capacity,
                                    size_t *length, uint32_t timeout_ms)
{
    if (transport == 0 || data == 0 || capacity == 0u || length == 0 ||
        transport->connected == 0u) {
        return ERR_DEVICE_NOT_READY;
    }
    *length = 0u;
    return transport->ops->receive(transport->context, data, capacity,
                                   length, timeout_ms);
}

status_t network_transport_close(network_transport_t *transport)
{
    status_t status;

    if (transport == 0 || transport->ops == 0) {
        return ERR_INVALID_ARG;
    }
    if (transport->initialized == 0u || transport->connected == 0u) {
        transport->connected = 0u;
        return SYS_OK;
    }
    status = transport->ops->close(transport->context);
    transport->connected = 0u;
    return status;
}

status_t network_transport_suspend(network_transport_t *transport)
{
    status_t status;

    if (transport == 0 || transport->initialized == 0u) {
        return ERR_DEVICE_NOT_READY;
    }
    (void)network_transport_close(transport);
    status = transport->ops->suspend != 0
        ? transport->ops->suspend(transport->context) : SYS_OK;
    if (status == SYS_OK) {
        transport->initialized = 0u;
    }
    return status;
}

status_t network_transport_resume(network_transport_t *transport)
{
    status_t status;

    if (transport == 0 || transport->ops == 0) {
        return ERR_INVALID_ARG;
    }
    status = transport->ops->resume != 0
        ? transport->ops->resume(transport->context)
        : transport->ops->init(transport->context);
    transport->initialized = status == SYS_OK ? 1u : 0u;
    transport->connected = 0u;
    return status;
}
