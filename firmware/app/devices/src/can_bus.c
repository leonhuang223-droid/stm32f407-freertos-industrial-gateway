#include "can_bus.h"

#include <string.h>

static status_t validate_bus(const can_bus_t *bus)
{
    return bus != 0 && bus->ops != 0 && bus->context != 0 ? SYS_OK
                                                          : ERR_INVALID_ARG;
}

status_t
can_bus_construct(can_bus_t *bus, const can_bus_ops_t *ops, void *context)
{
    if (bus == 0 || ops == 0 || context == 0 || ops->start == 0 ||
        ops->send == 0 || ops->receive == 0 || ops->wait_event == 0 ||
        ops->get_state == 0 || ops->recover == 0) {
        return ERR_INVALID_ARG;
    }
    memset(bus, 0, sizeof(*bus));
    bus->ops = ops;
    bus->context = context;
    return SYS_OK;
}

status_t can_bus_start(can_bus_t *bus)
{
    status_t status;

    if (validate_bus(bus) != SYS_OK) {
        return ERR_INVALID_ARG;
    }
    status = bus->ops->start(bus->context);
    if (status == SYS_OK) {
        bus->started = 1u;
        bus->suspended = 0u;
    }
    return status;
}

status_t can_bus_send(can_bus_t *bus, const can_frame_t *frame)
{
    if (validate_bus(bus) != SYS_OK || frame == 0 || frame->dlc > 8u) {
        return ERR_INVALID_ARG;
    }
    if (bus->started == 0u || bus->suspended != 0u) {
        return ERR_DEVICE_NOT_READY;
    }
    return bus->ops->send(bus->context, frame);
}

status_t can_bus_receive(can_bus_t *bus, can_frame_t *frame)
{
    if (validate_bus(bus) != SYS_OK || frame == 0) {
        return ERR_INVALID_ARG;
    }
    if (bus->started == 0u || bus->suspended != 0u) {
        return ERR_DEVICE_NOT_READY;
    }
    return bus->ops->receive(bus->context, frame);
}

status_t
can_bus_wait_event(can_bus_t *bus, uint32_t timeout_ms, uint32_t *event_bits)
{
    if (validate_bus(bus) != SYS_OK || event_bits == 0) {
        return ERR_INVALID_ARG;
    }
    if (bus->started == 0u || bus->suspended != 0u) {
        return ERR_DEVICE_NOT_READY;
    }
    *event_bits = CAN_BUS_EVENT_NONE;
    return bus->ops->wait_event(bus->context, timeout_ms, event_bits);
}

status_t can_bus_get_state(can_bus_t *bus, can_bus_state_t *state)
{
    if (validate_bus(bus) != SYS_OK || state == 0) {
        return ERR_INVALID_ARG;
    }
    return bus->ops->get_state(bus->context, state);
}

status_t can_bus_recover(can_bus_t *bus)
{
    status_t status;

    if (validate_bus(bus) != SYS_OK) {
        return ERR_INVALID_ARG;
    }
    status = bus->ops->recover(bus->context);
    if (status == SYS_OK) {
        bus->started = 1u;
        bus->suspended = 0u;
    }
    return status;
}

status_t can_bus_suspend(can_bus_t *bus)
{
    status_t status;

    if (validate_bus(bus) != SYS_OK) {
        return ERR_INVALID_ARG;
    }
    if (bus->started == 0u) {
        return ERR_DEVICE_NOT_READY;
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

status_t can_bus_resume(can_bus_t *bus)
{
    status_t status;

    if (validate_bus(bus) != SYS_OK) {
        return ERR_INVALID_ARG;
    }
    if (bus->started == 0u) {
        return ERR_DEVICE_NOT_READY;
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
