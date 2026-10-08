#include "network_transport.h"

#include <string.h>

static int transport_valid(const network_transport_t *transport)
{
    return transport != 0 && transport->ops != 0 && transport->context != 0 &&
           transport->ops->init != 0 && transport->ops->connect != 0 &&
           transport->ops->send != 0 && transport->ops->receive != 0 &&
           transport->ops->close != 0;
}

static int steps_valid(const network_connect_step_ops_t *steps)
{
    return steps != 0 && steps->init_step != 0 && steps->connect_step != 0 &&
           steps->cancel != 0;
}

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

status_t
network_transport_set_connect_steps(network_transport_t *transport,
                                    const network_connect_step_ops_t *steps)
{
    if (!transport_valid(transport) || !steps_valid(steps)) {
        return ERR_INVALID_ARG;
    }
    if (transport->initialized != 0u || transport->connected != 0u) {
        return ERR_DEVICE_NOT_READY;
    }
    transport->connect_steps = steps;
    return SYS_OK;
}

status_t network_transport_init(network_transport_t *transport)
{
    status_t status;

    if (!transport_valid(transport)) {
        return ERR_INVALID_ARG;
    }
    if (transport->connected != 0u) {
        return ERR_DEVICE_NOT_READY;
    }
    status = transport->ops->init(transport->context);
    transport->initialized = status == SYS_OK ? 1u : 0u;
    transport->connected = 0u;
    return status;
}

status_t network_transport_init_step(network_transport_t *transport,
                                     uint32_t now_ms)
{
    status_t status;
    if (!transport_valid(transport)) {
        return ERR_INVALID_ARG;
    }
    if (transport->connected != 0u) {
        return ERR_DEVICE_NOT_READY;
    }
    if (transport->connect_steps == 0) {
        return network_transport_init(transport);
    }
    if (!steps_valid(transport->connect_steps)) {
        return ERR_INVALID_ARG;
    }
    status = transport->connect_steps->init_step(transport->context, now_ms);
    if (status == SYS_OK) {
        transport->initialized = 1u;
        transport->connected = 0u;
    }
    return status;
}

status_t network_transport_connect_step(network_transport_t *transport,
                                        const char *host,
                                        uint16_t port,
                                        uint32_t now_ms)
{
    status_t status;
    if (!transport_valid(transport) || host == 0 || host[0] == '\0' ||
        port == 0u) {
        return ERR_INVALID_ARG;
    }
    if (transport->initialized == 0u || transport->connected != 0u) {
        return ERR_DEVICE_NOT_READY;
    }
    if (transport->connect_steps == 0) {
        return network_transport_connect(transport, host, port);
    }
    if (!steps_valid(transport->connect_steps)) {
        return ERR_INVALID_ARG;
    }
    status = transport->connect_steps->connect_step(
        transport->context, host, port, now_ms);
    if (status == SYS_OK) {
        transport->connected = 1u;
    }
    return status;
}

void network_transport_cancel_connect(network_transport_t *transport)
{
    if (transport_valid(transport) && steps_valid(transport->connect_steps)) {
        transport->connect_steps->cancel(transport->context);
        if (transport->connected == 0u) {
            transport->initialized = 0u;
        }
    }
}

status_t network_transport_connect(network_transport_t *transport,
                                   const char *host,
                                   uint16_t port)
{
    status_t status;

    if (!transport_valid(transport) || host == 0 || host[0] == '\0' ||
        port == 0u) {
        return ERR_INVALID_ARG;
    }
    if (transport->initialized == 0u || transport->connected != 0u) {
        return ERR_DEVICE_NOT_READY;
    }
    status = transport->ops->connect(transport->context, host, port);
    transport->connected = status == SYS_OK ? 1u : 0u;
    return status;
}

status_t network_transport_send(network_transport_t *transport,
                                const uint8_t *data,
                                size_t length)
{
    if (!transport_valid(transport) || data == 0 || length == 0u) {
        return ERR_INVALID_ARG;
    }
    if (transport->connected == 0u) {
        return ERR_DEVICE_NOT_READY;
    }
    return transport->ops->send(transport->context, data, length);
}

status_t network_transport_receive(network_transport_t *transport,
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
    if (transport->connected == 0u) {
        return ERR_DEVICE_NOT_READY;
    }
    status = transport->ops->receive(
        transport->context, data, capacity, length, timeout_ms);
    if (*length > capacity) {
        *length = 0u;
        return ERR_PROTOCOL;
    }
    if (status != SYS_OK) {
        *length = 0u;
    }
    return status;
}

status_t network_transport_close(network_transport_t *transport)
{
    status_t status;

    if (!transport_valid(transport)) {
        return ERR_INVALID_ARG;
    }
    if (transport->initialized == 0u || transport->connected == 0u) {
        transport->connected = 0u;
        return SYS_OK;
    }
    status = transport->ops->close(transport->context);
    if (status == SYS_OK) {
        transport->connected = 0u;
    }
    return status;
}

status_t network_transport_suspend(network_transport_t *transport)
{
    status_t status;

    if (!transport_valid(transport)) {
        return ERR_DEVICE_NOT_READY;
    }
    network_transport_cancel_connect(transport);
    status = network_transport_close(transport);
    if (status != SYS_OK) {
        return status;
    }
    status = transport->ops->suspend != 0
                 ? transport->ops->suspend(transport->context)
                 : SYS_OK;
    if (status == SYS_OK) {
        transport->initialized = 0u;
    }
    return status;
}

status_t network_transport_resume(network_transport_t *transport)
{
    status_t status;

    if (!transport_valid(transport)) {
        return ERR_INVALID_ARG;
    }
    if (transport->connected != 0u) {
        return ERR_DEVICE_NOT_READY;
    }
    status = transport->ops->resume != 0
                 ? transport->ops->resume(transport->context)
                 : transport->ops->init(transport->context);
    transport->initialized = status == SYS_OK ? 1u : 0u;
    transport->connected = 0u;
    return status;
}

uint8_t network_transport_is_initialized(const network_transport_t *transport)
{
    return transport_valid(transport) ? transport->initialized : 0u;
}

uint8_t network_transport_is_connected(const network_transport_t *transport)
{
    return transport_valid(transport) ? transport->connected : 0u;
}

uint8_t
network_transport_has_connect_steps(const network_transport_t *transport)
{
    return transport_valid(transport) && steps_valid(transport->connect_steps)
               ? 1u
               : 0u;
}
