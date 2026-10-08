#ifndef GATEWAY_FIELDBUS_SUBSYSTEM_H
#define GATEWAY_FIELDBUS_SUBSYSTEM_H

#include "can_bus.h"
#include "modbus_rtu_master.h"

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t can_rx_frames;
    uint32_t can_tx_frames;
    uint32_t can_protocol_errors;
    uint32_t can_tx_errors;
    uint32_t can_bus_off_events;
    uint32_t can_recoveries;
    status_t last_can_error;
} fieldbus_health_t;

typedef struct {
    modbus_master_t *modbus;
    can_bus_t *can_bus;
    uint32_t can_recovery_delay_ms;
    uint32_t can_recovery_due_ms;
    uint8_t local_can_node_id;
    uint8_t can_started;
    uint8_t can_bus_off_latched;
    fieldbus_health_t health;
} fieldbus_subsystem_t;

status_t fieldbus_subsystem_construct(fieldbus_subsystem_t *subsystem,
                                      modbus_master_t *modbus,
                                      can_bus_t *can_bus,
                                      uint8_t local_can_node_id,
                                      uint32_t can_recovery_delay_ms);
status_t fieldbus_subsystem_start(fieldbus_subsystem_t *subsystem);
status_t fieldbus_subsystem_poll_modbus(fieldbus_subsystem_t *subsystem,
                                        uint32_t now_ms,
                                        gateway_measurement_t *measurement);
status_t fieldbus_subsystem_wait_can(fieldbus_subsystem_t *subsystem,
                                     uint32_t timeout_ms,
                                     uint32_t *event_bits);
/** Synchronous request; pointed-to buffers remain caller-owned.
 * @author 兆鸣嵌入式
 */
typedef struct {
    uint32_t event_bits;
    uint32_t now_ms;
    gateway_measurement_t *measurements;
    size_t capacity;
    size_t *count;
} fieldbus_can_process_t;

status_t
fieldbus_subsystem_process_can(fieldbus_subsystem_t *subsystem,
                               const fieldbus_can_process_t *parameters);
status_t fieldbus_subsystem_send_can(fieldbus_subsystem_t *subsystem,
                                     const gateway_measurement_t *measurement);

#endif
