#ifndef GATEWAY_CLI_TRANSPORT_H
#define GATEWAY_CLI_TRANSPORT_H

#include "error_code.h"

#include <stddef.h>
#include <stdint.h>

typedef struct {
    status_t (*init)(void *context);
    status_t (*read)(void *context, uint8_t *data, size_t capacity,
                     size_t *length, uint32_t timeout_ms);
    status_t (*write)(void *context, const uint8_t *data, size_t length,
                      uint32_t timeout_ms);
    status_t (*suspend)(void *context);
    status_t (*resume)(void *context);
} cli_transport_ops_t;

typedef struct {
    const cli_transport_ops_t *ops;
    void *context;
    uint32_t rx_bytes;
    uint32_t tx_bytes;
    uint32_t errors;
    status_t last_error;
    uint8_t initialized;
    uint8_t suspended;
} cli_transport_t;

status_t cli_transport_construct(cli_transport_t *transport,
                                 const cli_transport_ops_t *ops,
                                 void *context);
status_t cli_transport_init(cli_transport_t *transport);
status_t cli_transport_read(cli_transport_t *transport, uint8_t *data,
                            size_t capacity, size_t *length,
                            uint32_t timeout_ms);
status_t cli_transport_write(cli_transport_t *transport,
                             const uint8_t *data, size_t length,
                             uint32_t timeout_ms);
status_t cli_transport_suspend(cli_transport_t *transport);
status_t cli_transport_resume(cli_transport_t *transport);

#endif
