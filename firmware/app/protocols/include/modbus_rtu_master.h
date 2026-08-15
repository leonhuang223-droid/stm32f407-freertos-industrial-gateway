#ifndef GATEWAY_MODBUS_RTU_MASTER_H
#define GATEWAY_MODBUS_RTU_MASTER_H

#include "gateway_model.h"
#include "rs485_bus.h"

#include <stddef.h>
#include <stdint.h>

enum {
    MODBUS_RTU_MAX_REGISTERS = 16,
    MODBUS_RTU_MAX_ADU_SIZE = 64
};

typedef enum {
    MODBUS_FUNCTION_READ_HOLDING = 0x03,
    MODBUS_FUNCTION_READ_INPUT = 0x04
} modbus_function_t;

typedef enum {
    MODBUS_VALUE_U16 = 0,
    MODBUS_VALUE_S16,
    MODBUS_VALUE_U32,
    MODBUS_VALUE_S32
} modbus_value_type_t;

typedef enum {
    MODBUS_WORD_HIGH_FIRST = 0,
    MODBUS_WORD_LOW_FIRST
} modbus_word_order_t;

typedef struct {
    uint8_t slave_id;
    modbus_function_t function;
    uint16_t start_address;
    uint16_t quantity;
    uint16_t value_register_index;
    modbus_value_type_t value_type;
    modbus_word_order_t word_order;
    int32_t scale_numerator;
    int32_t scale_denominator;
    int32_t engineering_offset;
    uint16_t point_id;
    gateway_measurement_unit_t unit;
} modbus_poll_entry_t;

typedef struct {
    uint32_t request_id;
    uint32_t polls;
    uint32_t transport_attempts;
    uint32_t retries;
    uint32_t timeouts;
    uint32_t crc_errors;
    uint32_t protocol_errors;
    uint32_t exceptions;
    status_t last_error;
} modbus_master_health_t;

typedef struct {
    rs485_bus_t *bus;
    const modbus_poll_entry_t *poll_table;
    size_t poll_count;
    size_t next_poll;
    uint32_t measurement_sequence;
    uint8_t retry_limit;
    modbus_master_health_t health;
} modbus_master_t;

uint16_t modbus_rtu_crc16(const uint8_t *data, size_t length);
status_t modbus_master_construct(modbus_master_t *master, rs485_bus_t *bus,
                                 const modbus_poll_entry_t *poll_table,
                                 size_t poll_count, uint8_t retry_limit);
status_t modbus_master_poll_next(modbus_master_t *master, uint32_t now_ms,
                                 gateway_measurement_t *measurement);

#endif
