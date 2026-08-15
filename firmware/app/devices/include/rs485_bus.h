#ifndef GATEWAY_RS485_BUS_H
#define GATEWAY_RS485_BUS_H

#include "error_code.h"

#include <stddef.h>
#include <stdint.h>

typedef struct rs485_bus rs485_bus_t;

/**
 * Blocking transaction boundary owned by the Modbus task. Platform adapters
 * may use DMA and task notifications internally, but must return before the
 * response buffer is released by the caller.
 */
typedef struct {
    status_t (*exchange)(void *context,
                         const uint8_t *request, size_t request_length,
                         uint8_t *response, size_t response_capacity,
                         size_t *response_length, uint32_t timeout_ms);
    status_t (*suspend)(void *context);
    status_t (*resume)(void *context);
} rs485_bus_ops_t;

struct rs485_bus {
    const rs485_bus_ops_t *ops;
    void *context;
    uint32_t timeout_ms;
    uint8_t suspended;
};

status_t rs485_bus_construct(rs485_bus_t *bus,
                             const rs485_bus_ops_t *ops,
                             void *context, uint32_t timeout_ms);
status_t rs485_bus_exchange(rs485_bus_t *bus,
                            const uint8_t *request, size_t request_length,
                            uint8_t *response, size_t response_capacity,
                            size_t *response_length);
status_t rs485_bus_suspend(rs485_bus_t *bus);
status_t rs485_bus_resume(rs485_bus_t *bus);

#endif
