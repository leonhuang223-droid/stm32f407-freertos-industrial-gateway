#include "fieldbus_subsystem.h"

#include "can_protocol.h"

#include <string.h>

static int time_reached(uint32_t now_ms, uint32_t deadline_ms)
{
    return (int32_t)(now_ms - deadline_ms) >= 0;
}

status_t fieldbus_subsystem_construct(fieldbus_subsystem_t *subsystem,
                                      modbus_master_t *modbus,
                                      can_bus_t *can_bus,
                                      uint8_t local_can_node_id,
                                      uint32_t can_recovery_delay_ms)
{
    if (subsystem == 0 || modbus == 0 || can_bus == 0 ||
        local_can_node_id == 0u || local_can_node_id > 127u ||
        can_recovery_delay_ms == 0u) {
        return ERR_INVALID_ARG;
    }
    memset(subsystem, 0, sizeof(*subsystem));
    subsystem->modbus = modbus;
    subsystem->can_bus = can_bus;
    subsystem->local_can_node_id = local_can_node_id;
    subsystem->can_recovery_delay_ms = can_recovery_delay_ms;
    subsystem->health.last_can_error = ERR_DEVICE_NOT_READY;
    return SYS_OK;
}

status_t fieldbus_subsystem_start(fieldbus_subsystem_t *subsystem)
{
    status_t status;

    if (subsystem == 0 || subsystem->can_bus == 0) {
        return ERR_INVALID_ARG;
    }
    status = can_bus_start(subsystem->can_bus);
    subsystem->can_started = status == SYS_OK ? 1u : 0u;
    subsystem->health.last_can_error = status;
    return status;
}

status_t fieldbus_subsystem_poll_modbus(fieldbus_subsystem_t *subsystem,
                                        uint32_t now_ms,
                                        gateway_measurement_t *measurement)
{
    if (subsystem == 0 || subsystem->modbus == 0) {
        return ERR_INVALID_ARG;
    }
    return modbus_master_poll_next(subsystem->modbus, now_ms, measurement);
}

status_t fieldbus_subsystem_wait_can(fieldbus_subsystem_t *subsystem,
                                     uint32_t timeout_ms,
                                     uint32_t *event_bits)
{
    if (subsystem == 0 || event_bits == 0) {
        return ERR_INVALID_ARG;
    }
    if (subsystem->can_started == 0u) {
        *event_bits = CAN_BUS_EVENT_ERROR;
        return ERR_DEVICE_NOT_READY;
    }
    return can_bus_wait_event(subsystem->can_bus, timeout_ms, event_bits);
}

static status_t maintain_can_state(fieldbus_subsystem_t *subsystem,
                                   uint32_t now_ms)
{
    can_bus_state_t state = CAN_BUS_STOPPED;
    status_t status;

    if (subsystem->can_started == 0u) {
        if (!time_reached(now_ms, subsystem->can_recovery_due_ms)) {
            return ERR_DEVICE_NOT_READY;
        }
        status = can_bus_start(subsystem->can_bus);
        if (status == SYS_OK) {
            subsystem->can_started = 1u;
            subsystem->health.can_recoveries++;
            subsystem->health.last_can_error = SYS_OK;
        } else {
            subsystem->can_recovery_due_ms = now_ms +
                subsystem->can_recovery_delay_ms;
            subsystem->health.last_can_error = status;
        }
        return status;
    }
    status = can_bus_get_state(subsystem->can_bus, &state);
    if (status != SYS_OK) {
        subsystem->health.last_can_error = status;
        return status;
    }
    if (state != CAN_BUS_OFF) {
        subsystem->can_bus_off_latched = 0u;
        return SYS_OK;
    }
    if (subsystem->can_bus_off_latched == 0u) {
        subsystem->can_bus_off_latched = 1u;
        subsystem->can_recovery_due_ms = now_ms +
            subsystem->can_recovery_delay_ms;
        subsystem->health.can_bus_off_events++;
    }
    if (!time_reached(now_ms, subsystem->can_recovery_due_ms)) {
        subsystem->health.last_can_error = ERR_BUS_OFF;
        return ERR_BUS_OFF;
    }
    status = can_bus_recover(subsystem->can_bus);
    if (status == SYS_OK) {
        subsystem->can_bus_off_latched = 0u;
        subsystem->health.can_recoveries++;
    } else {
        subsystem->can_recovery_due_ms = now_ms +
            subsystem->can_recovery_delay_ms;
    }
    subsystem->health.last_can_error = status;
    return status;
}

status_t fieldbus_subsystem_process_can(fieldbus_subsystem_t *subsystem,
                                        uint32_t event_bits, uint32_t now_ms,
                                        gateway_measurement_t *measurements,
                                        size_t capacity, size_t *count)
{
    status_t maintenance_status;
    status_t first_error = SYS_OK;

    if (subsystem == 0 || measurements == 0 || capacity == 0u || count == 0) {
        return ERR_INVALID_ARG;
    }
    *count = 0u;
    maintenance_status = maintain_can_state(subsystem, now_ms);
    if (maintenance_status != SYS_OK) {
        return maintenance_status;
    }
    if ((event_bits & (CAN_BUS_EVENT_RX | CAN_BUS_EVENT_ERROR)) == 0u) {
        return SYS_OK;
    }
    while (*count < capacity) {
        can_frame_t frame;
        gateway_measurement_t measurement;
        uint8_t source_node = 0u;
        status_t status = can_bus_receive(subsystem->can_bus, &frame);

        if (status == ERR_DEVICE_NOT_READY) {
            break;
        }
        if (status != SYS_OK) {
            first_error = status;
            subsystem->health.last_can_error = status;
            break;
        }
        subsystem->health.can_rx_frames++;
        status = can_protocol_decode_measurement(&frame, now_ms,
                                                 &source_node, &measurement);
        if (status == ERR_UNSUPPORTED || source_node ==
            subsystem->local_can_node_id) {
            continue;
        }
        if (status != SYS_OK) {
            subsystem->health.can_protocol_errors++;
            if (first_error == SYS_OK) {
                first_error = status;
            }
            continue;
        }
        measurements[*count] = measurement;
        (*count)++;
    }
    subsystem->health.last_can_error = first_error;
    return first_error;
}

status_t fieldbus_subsystem_send_can(fieldbus_subsystem_t *subsystem,
                                     const gateway_measurement_t *measurement)
{
    can_frame_t frame;
    status_t status;

    if (subsystem == 0 || measurement == 0 || subsystem->can_started == 0u) {
        return ERR_DEVICE_NOT_READY;
    }
    status = can_protocol_encode_measurement(subsystem->local_can_node_id,
                                             measurement, &frame);
    if (status == SYS_OK) {
        status = can_bus_send(subsystem->can_bus, &frame);
    }
    if (status == SYS_OK) {
        subsystem->health.can_tx_frames++;
    } else {
        subsystem->health.can_tx_errors++;
        subsystem->health.last_can_error = status;
    }
    return status;
}
