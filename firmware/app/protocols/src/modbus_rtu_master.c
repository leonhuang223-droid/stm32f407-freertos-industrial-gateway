#include "modbus_rtu_master.h"

#include <limits.h>
#include <string.h>

uint16_t modbus_rtu_crc16(const uint8_t *data, size_t length)
{
    uint16_t crc = 0xffffu;
    size_t i;

    if (data == 0) {
        return 0u;
    }
    for (i = 0u; i < length; ++i) {
        uint8_t bit;

        crc ^= data[i];
        for (bit = 0u; bit < 8u; ++bit) {
            crc = (crc & 1u) != 0u ? (uint16_t)((crc >> 1u) ^ 0xa001u)
                                   : (uint16_t)(crc >> 1u);
        }
    }
    return crc;
}

static int value_register_count(modbus_value_type_t type)
{
    return type == MODBUS_VALUE_U32 || type == MODBUS_VALUE_S32 ? 2 : 1;
}

static status_t validate_entry(const modbus_poll_entry_t *entry)
{
    uint32_t required_registers;

    if (entry == 0 || entry->slave_id == 0u || entry->slave_id > 247u ||
        (entry->function != MODBUS_FUNCTION_READ_HOLDING &&
         entry->function != MODBUS_FUNCTION_READ_INPUT) ||
        entry->quantity == 0u || entry->quantity > MODBUS_RTU_MAX_REGISTERS ||
        entry->scale_denominator == 0 || entry->point_id == 0u ||
        entry->unit > GATEWAY_UNIT_RAW ||
        entry->value_type > MODBUS_VALUE_S32 ||
        entry->word_order > MODBUS_WORD_LOW_FIRST) {
        return ERR_INVALID_ARG;
    }
    required_registers = (uint32_t)entry->value_register_index +
                         (uint32_t)value_register_count(entry->value_type);
    return required_registers <= entry->quantity ? SYS_OK : ERR_INVALID_ARG;
}

status_t modbus_master_construct(modbus_master_t *master,
                                 rs485_bus_t *bus,
                                 const modbus_poll_entry_t *poll_table,
                                 size_t poll_count,
                                 uint8_t retry_limit)
{
    size_t i;

    if (master == 0 || bus == 0 || poll_table == 0 || poll_count == 0u) {
        return ERR_INVALID_ARG;
    }
    for (i = 0u; i < poll_count; ++i) {
        if (validate_entry(&poll_table[i]) != SYS_OK) {
            return ERR_INVALID_ARG;
        }
    }
    memset(master, 0, sizeof(*master));
    master->bus = bus;
    master->poll_table = poll_table;
    master->poll_count = poll_count;
    master->retry_limit = retry_limit;
    master->health.last_error = ERR_DEVICE_NOT_READY;
    return SYS_OK;
}

static void build_request(const modbus_poll_entry_t *entry, uint8_t request[8])
{
    uint16_t crc;

    request[0] = entry->slave_id;
    request[1] = (uint8_t)entry->function;
    request[2] = (uint8_t)(entry->start_address >> 8u);
    request[3] = (uint8_t)entry->start_address;
    request[4] = (uint8_t)(entry->quantity >> 8u);
    request[5] = (uint8_t)entry->quantity;
    crc = modbus_rtu_crc16(request, 6u);
    request[6] = (uint8_t)crc;
    request[7] = (uint8_t)(crc >> 8u);
}

static status_t response_crc_status(const uint8_t *response, size_t length)
{
    uint16_t expected;

    if (response == 0 || length < 5u) {
        return ERR_PROTOCOL;
    }
    expected = (uint16_t)response[length - 2u] |
               (uint16_t)((uint16_t)response[length - 1u] << 8u);
    return modbus_rtu_crc16(response, length - 2u) == expected ? SYS_OK
                                                               : ERR_CRC;
}

static uint16_t response_word(const uint8_t *data, size_t register_index)
{
    size_t offset = register_index * 2u;

    return (uint16_t)((uint16_t)data[offset] << 8u) | data[offset + 1u];
}

static status_t decode_raw_value(const modbus_poll_entry_t *entry,
                                 const uint8_t *data,
                                 int32_t *raw_value)
{
    uint16_t first;

    if (entry == 0 || data == 0 || raw_value == 0) {
        return ERR_INVALID_ARG;
    }
    first = response_word(data, entry->value_register_index);
    if (entry->value_type == MODBUS_VALUE_U16) {
        *raw_value = first;
        return SYS_OK;
    }
    if (entry->value_type == MODBUS_VALUE_S16) {
        *raw_value = (int16_t)first;
        return SYS_OK;
    }
    {
        uint16_t second =
            response_word(data, (size_t)entry->value_register_index + 1u);
        uint32_t combined = entry->word_order == MODBUS_WORD_HIGH_FIRST
                                ? ((uint32_t)first << 16u) | second
                                : ((uint32_t)second << 16u) | first;

        if (entry->value_type == MODBUS_VALUE_U32 && combined > INT32_MAX) {
            return ERR_PROTOCOL;
        }
        *raw_value = (int32_t)combined;
    }
    return SYS_OK;
}

static status_t parse_response(const modbus_poll_entry_t *entry,
                               const uint8_t *response,
                               size_t length,
                               int32_t *raw_value,
                               int32_t *engineering_value)
{
    size_t expected_length;
    status_t status;
    int64_t scaled;

    status = response_crc_status(response, length);
    if (status != SYS_OK) {
        return status;
    }
    if (response[0] != entry->slave_id) {
        return ERR_PROTOCOL;
    }
    if (response[1] == ((uint8_t)entry->function | 0x80u)) {
        return ERR_PROTOCOL;
    }
    expected_length = 5u + (size_t)entry->quantity * 2u;
    if (response[1] != (uint8_t)entry->function ||
        response[2] != entry->quantity * 2u || length != expected_length) {
        return ERR_PROTOCOL;
    }
    status = decode_raw_value(entry, &response[3], raw_value);
    if (status != SYS_OK) {
        return status;
    }
    scaled = ((int64_t)*raw_value * entry->scale_numerator) /
                 entry->scale_denominator +
             entry->engineering_offset;
    if (scaled < INT32_MIN || scaled > INT32_MAX) {
        return ERR_PROTOCOL;
    }
    *engineering_value = (int32_t)scaled;
    return SYS_OK;
}

static void update_error_statistics(modbus_master_t *master, status_t status)
{
    if (status == ERR_TIMEOUT) {
        master->health.timeouts++;
    } else if (status == ERR_CRC) {
        master->health.crc_errors++;
    } else if (status == ERR_PROTOCOL) {
        master->health.protocol_errors++;
    }
}

status_t modbus_master_poll_next(modbus_master_t *master,
                                 uint32_t now_ms,
                                 gateway_measurement_t *measurement)
{
    const modbus_poll_entry_t *entry;
    uint8_t request[8];
    uint8_t response[MODBUS_RTU_MAX_ADU_SIZE];
    uint8_t attempt;
    status_t status = ERR_DEVICE_NOT_READY;

    if (master == 0 || master->bus == 0 || measurement == 0 ||
        master->poll_table == 0 || master->poll_count == 0u) {
        return ERR_INVALID_ARG;
    }
    entry = &master->poll_table[master->next_poll];
    memset(measurement, 0, sizeof(*measurement));
    measurement->point_id = entry->point_id;
    measurement->source = GATEWAY_SOURCE_MODBUS;
    measurement->unit = (uint8_t)entry->unit;
    measurement->sequence = ++master->measurement_sequence;
    measurement->monotonic_ms = now_ms;
    measurement->quality = GATEWAY_QUALITY_COMM_ERROR;
    measurement->error = ERR_DEVICE_NOT_READY;

    master->health.request_id++;
    master->health.polls++;
    build_request(entry, request);
    for (attempt = 0u; attempt <= master->retry_limit; ++attempt) {
        size_t response_length = 0u;

        if (attempt != 0u) {
            master->health.retries++;
        }
        master->health.transport_attempts++;
        status =
            rs485_bus_exchange(master->bus,
                               &(const rs485_transfer_t){request,
                                                         sizeof(request),
                                                         response,
                                                         sizeof(response),
                                                         &response_length});
        if (status == SYS_OK) {
            status = parse_response(entry,
                                    response,
                                    response_length,
                                    &measurement->raw_value,
                                    &measurement->engineering_value);
            if (status == ERR_PROTOCOL && response_length == 5u &&
                response[1] == ((uint8_t)entry->function | 0x80u)) {
                master->health.exceptions++;
            }
        }
        if (status == SYS_OK) {
            break;
        }
        update_error_statistics(master, status);
    }
    master->next_poll = (master->next_poll + 1u) % master->poll_count;
    master->health.last_error = status;
    measurement->error = status;
    if (status == SYS_OK) {
        measurement->quality = GATEWAY_QUALITY_GOOD;
    }
    return status;
}
