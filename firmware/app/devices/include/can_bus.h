#ifndef GATEWAY_CAN_BUS_H
#define GATEWAY_CAN_BUS_H

#include "error_code.h"

#include <stdint.h>

enum {
    CAN_BUS_EVENT_NONE = 0u,
    CAN_BUS_EVENT_RX = 1u << 0,
    CAN_BUS_EVENT_ERROR = 1u << 1,
    CAN_BUS_EVENT_TX = 1u << 2
};

typedef enum {
    CAN_BUS_STOPPED = 0,
    CAN_BUS_ACTIVE,
    CAN_BUS_ERROR_WARNING,
    CAN_BUS_ERROR_PASSIVE,
    CAN_BUS_OFF
} can_bus_state_t;

typedef struct {
    uint32_t id;
    uint8_t is_extended;
    uint8_t is_remote;
    uint8_t dlc;
    uint8_t data[8];
} can_frame_t;

typedef struct {
    status_t (*start)(void *context);
    status_t (*send)(void *context, const can_frame_t *frame);
    status_t (*receive)(void *context, can_frame_t *frame);
    status_t (*wait_event)(void *context,
                           uint32_t timeout_ms,
                           uint32_t *event_bits);
    status_t (*get_state)(void *context, can_bus_state_t *state);
    status_t (*recover)(void *context);
    status_t (*suspend)(void *context);
    status_t (*resume)(void *context);
} can_bus_ops_t;

typedef struct {
    const can_bus_ops_t *ops;
    void *context;
    uint8_t started;
    uint8_t suspended;
} can_bus_t;

status_t
can_bus_construct(can_bus_t *bus, const can_bus_ops_t *ops, void *context);
status_t can_bus_start(can_bus_t *bus);
status_t can_bus_send(can_bus_t *bus, const can_frame_t *frame);
status_t can_bus_receive(can_bus_t *bus, can_frame_t *frame);
status_t
can_bus_wait_event(can_bus_t *bus, uint32_t timeout_ms, uint32_t *event_bits);
status_t can_bus_get_state(can_bus_t *bus, can_bus_state_t *state);
status_t can_bus_recover(can_bus_t *bus);
status_t can_bus_suspend(can_bus_t *bus);
status_t can_bus_resume(can_bus_t *bus);

#endif
