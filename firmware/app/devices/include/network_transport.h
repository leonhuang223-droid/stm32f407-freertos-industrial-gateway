#ifndef GATEWAY_NETWORK_TRANSPORT_H
#define GATEWAY_NETWORK_TRANSPORT_H

#include "error_code.h"

#include <stddef.h>
#include <stdint.h>

/* Callbacks run synchronously in the network owner task. Implementations must
 * honor receive capacity and use bounded hardware waits. A failed close keeps
 * the connection owned; retry close before initializing or connecting again. */
typedef struct {
    status_t (*init)(void *context);
    status_t (*connect)(void *context, const char *host, uint16_t port);
    status_t (*send)(void *context, const uint8_t *data, size_t length);
    status_t (*receive)(void *context,
                        uint8_t *data,
                        size_t capacity,
                        size_t *length,
                        uint32_t timeout_ms);
    status_t (*close)(void *context);
    status_t (*suspend)(void *context);
    status_t (*resume)(void *context);
} network_transport_ops_t;

typedef struct {
    status_t (*init_step)(void *context, uint32_t now_ms);
    status_t (*connect_step)(void *context,
                             const char *host,
                             uint16_t port,
                             uint32_t now_ms);
    void (*cancel)(void *context);
} network_connect_step_ops_t;

typedef struct {
    const network_transport_ops_t *ops;
    const network_connect_step_ops_t *connect_steps;
    void *context;
    uint8_t initialized;
    uint8_t connected;
} network_transport_t;

status_t network_transport_construct(network_transport_t *transport,
                                     const network_transport_ops_t *ops,
                                     void *context);
/* Configure before initialization; all step callbacks are mandatory. */
status_t
network_transport_set_connect_steps(network_transport_t *transport,
                                    const network_connect_step_ops_t *steps);
status_t network_transport_init(network_transport_t *transport);
status_t network_transport_init_step(network_transport_t *transport,
                                     uint32_t now_ms);
status_t network_transport_connect_step(network_transport_t *transport,
                                        const char *host,
                                        uint16_t port,
                                        uint32_t now_ms);
void network_transport_cancel_connect(network_transport_t *transport);
status_t network_transport_connect(network_transport_t *transport,
                                   const char *host,
                                   uint16_t port);
status_t network_transport_send(network_transport_t *transport,
                                const uint8_t *data,
                                size_t length);
status_t network_transport_receive(network_transport_t *transport,
                                   uint8_t *data,
                                   size_t capacity,
                                   size_t *length,
                                   uint32_t timeout_ms);
status_t network_transport_close(network_transport_t *transport);
status_t network_transport_suspend(network_transport_t *transport);
status_t network_transport_resume(network_transport_t *transport);
uint8_t network_transport_is_initialized(const network_transport_t *transport);
uint8_t network_transport_is_connected(const network_transport_t *transport);
uint8_t
network_transport_has_connect_steps(const network_transport_t *transport);

#endif
