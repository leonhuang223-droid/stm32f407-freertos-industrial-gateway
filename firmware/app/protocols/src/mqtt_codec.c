#include "mqtt_codec.h"

#include <string.h>

static status_t encode_remaining_length(uint32_t value,
                                        uint8_t *buffer,
                                        size_t capacity,
                                        size_t *length)
{
    size_t used = 0u;

    if (buffer == 0 || length == 0 || value > MQTT_MAX_REMAINING_LENGTH) {
        return ERR_INVALID_ARG;
    }
    do {
        uint8_t digit;

        if (used >= capacity || used >= 4u) {
            return ERR_NO_MEMORY;
        }
        digit = (uint8_t)(value % 128u);
        value /= 128u;
        if (value != 0u) {
            digit |= 0x80u;
        }
        buffer[used++] = digit;
    } while (value != 0u);
    *length = used;
    return SYS_OK;
}

static status_t append_u16_string(uint8_t *buffer,
                                  size_t capacity,
                                  size_t *offset,
                                  const char *value)
{
    size_t length;

    if (buffer == 0 || offset == 0 || value == 0) {
        return ERR_INVALID_ARG;
    }
    length = strlen(value);
    if (length > UINT16_MAX || *offset > capacity ||
        capacity - *offset < length + 2u) {
        return ERR_NO_MEMORY;
    }
    buffer[(*offset)++] = (uint8_t)(length >> 8u);
    buffer[(*offset)++] = (uint8_t)length;
    memcpy(&buffer[*offset], value, length);
    *offset += length;
    return SYS_OK;
}

status_t mqtt_encode_connect(const mqtt_connect_options_t *options,
                             uint8_t *buffer,
                             size_t capacity,
                             size_t *length)
{
    uint8_t body[512];
    uint8_t remaining[4];
    uint8_t flags = 0u;
    size_t body_length = 0u;
    size_t remaining_length = 0u;
    status_t status;

    if (options == 0 || buffer == 0 || length == 0 || options->client_id == 0 ||
        options->client_id[0] == '\0' ||
        (options->password != 0 && options->password[0] != '\0' &&
         (options->username == 0 || options->username[0] == '\0'))) {
        return ERR_INVALID_ARG;
    }
    status = append_u16_string(body, sizeof(body), &body_length, "MQTT");
    if (status != SYS_OK) {
        return status;
    }
    body[body_length++] = 4u;
    if (options->clean_session != 0u) {
        flags |= 0x02u;
    }
    if (options->username != 0 && options->username[0] != '\0') {
        flags |= 0x80u;
    }
    if (options->password != 0 && options->password[0] != '\0') {
        flags |= 0x40u;
    }
    body[body_length++] = flags;
    body[body_length++] = (uint8_t)(options->keep_alive_seconds >> 8u);
    body[body_length++] = (uint8_t)options->keep_alive_seconds;
    status =
        append_u16_string(body, sizeof(body), &body_length, options->client_id);
    if (status == SYS_OK && (flags & 0x80u) != 0u) {
        status = append_u16_string(
            body, sizeof(body), &body_length, options->username);
    }
    if (status == SYS_OK && (flags & 0x40u) != 0u) {
        status = append_u16_string(
            body, sizeof(body), &body_length, options->password);
    }
    if (status != SYS_OK) {
        return status;
    }
    status = encode_remaining_length(
        (uint32_t)body_length, remaining, sizeof(remaining), &remaining_length);
    if (status != SYS_OK || capacity < 1u + remaining_length + body_length) {
        return status == SYS_OK ? ERR_NO_MEMORY : status;
    }
    buffer[0] = 0x10u;
    memcpy(&buffer[1], remaining, remaining_length);
    memcpy(&buffer[1u + remaining_length], body, body_length);
    *length = 1u + remaining_length + body_length;
    return SYS_OK;
}

status_t mqtt_encode_publish_qos1(const char *topic,
                                  const mqtt_publish_request_t *parameters)
{
    if (parameters == 0) {
        return ERR_INVALID_ARG;
    }
    const uint8_t *payload = parameters->payload;
    size_t payload_length = parameters->payload_length;
    uint16_t packet_id = parameters->packet_id;
    uint8_t duplicate = parameters->duplicate;
    uint8_t *buffer = parameters->buffer;
    size_t capacity = parameters->capacity;
    size_t *length = parameters->length;

    uint8_t remaining[4];
    size_t topic_length;
    size_t remaining_bytes = 0u;
    size_t offset;
    uint32_t remaining_value;
    status_t status;

    if (topic == 0 || topic[0] == '\0' || payload == 0 ||
        payload_length == 0u || packet_id == 0u || buffer == 0 || length == 0) {
        return ERR_INVALID_ARG;
    }
    topic_length = strlen(topic);
    if (topic_length > UINT16_MAX ||
        payload_length > MQTT_MAX_REMAINING_LENGTH - topic_length - 4u) {
        return ERR_INVALID_ARG;
    }
    remaining_value = (uint32_t)(topic_length + payload_length + 4u);
    status = encode_remaining_length(
        remaining_value, remaining, sizeof(remaining), &remaining_bytes);
    if (status != SYS_OK || capacity < 1u + remaining_bytes + remaining_value) {
        return status == SYS_OK ? ERR_NO_MEMORY : status;
    }
    buffer[0] = (uint8_t)(0x32u | (duplicate != 0u ? 0x08u : 0u));
    memcpy(&buffer[1], remaining, remaining_bytes);
    offset = 1u + remaining_bytes;
    buffer[offset++] = (uint8_t)(topic_length >> 8u);
    buffer[offset++] = (uint8_t)topic_length;
    memcpy(&buffer[offset], topic, topic_length);
    offset += topic_length;
    buffer[offset++] = (uint8_t)(packet_id >> 8u);
    buffer[offset++] = (uint8_t)packet_id;
    memcpy(&buffer[offset], payload, payload_length);
    offset += payload_length;
    *length = offset;
    return SYS_OK;
}

static status_t encode_two_byte_packet(uint8_t type,
                                       uint8_t *buffer,
                                       size_t capacity,
                                       size_t *length)
{
    if (buffer == 0 || length == 0) {
        return ERR_INVALID_ARG;
    }
    if (capacity < 2u) {
        return ERR_NO_MEMORY;
    }
    buffer[0] = type;
    buffer[1] = 0u;
    *length = 2u;
    return SYS_OK;
}

status_t mqtt_encode_pingreq(uint8_t *buffer, size_t capacity, size_t *length)
{
    return encode_two_byte_packet(0xc0u, buffer, capacity, length);
}

status_t
mqtt_encode_disconnect(uint8_t *buffer, size_t capacity, size_t *length)
{
    return encode_two_byte_packet(0xe0u, buffer, capacity, length);
}

static status_t decode_remaining_length(const uint8_t *buffer,
                                        size_t length,
                                        uint32_t *value,
                                        size_t *used)
{
    uint32_t multiplier = 1u;
    uint32_t result = 0u;
    size_t index;

    if (buffer == 0 || value == 0 || used == 0) {
        return ERR_INVALID_ARG;
    }
    for (index = 0u; index < length && index < 4u; ++index) {
        uint8_t digit = buffer[index];

        result += (uint32_t)(digit & 0x7fu) * multiplier;
        if ((digit & 0x80u) == 0u) {
            *value = result;
            *used = index + 1u;
            return SYS_OK;
        }
        multiplier *= 128u;
    }
    return length < 4u ? ERR_DEVICE_NOT_READY : ERR_PROTOCOL;
}

status_t mqtt_decode_packet(const uint8_t *buffer,
                            size_t length,
                            mqtt_packet_view_t *packet,
                            size_t *consumed)
{
    uint32_t remaining_length;
    size_t remaining_bytes;
    size_t total_length;
    uint8_t type;
    const uint8_t *payload;
    status_t status;

    if (buffer == 0 || packet == 0 || consumed == 0) {
        return ERR_INVALID_ARG;
    }
    if (length < 2u) {
        return ERR_DEVICE_NOT_READY;
    }
    status = decode_remaining_length(
        &buffer[1], length - 1u, &remaining_length, &remaining_bytes);
    if (status != SYS_OK) {
        return status;
    }
    total_length = 1u + remaining_bytes + remaining_length;
    if (total_length > length) {
        return ERR_DEVICE_NOT_READY;
    }
    memset(packet, 0, sizeof(*packet));
    type = buffer[0] >> 4u;
    packet->type = (mqtt_packet_type_t)type;
    packet->flags = buffer[0] & 0x0fu;
    payload = &buffer[1u + remaining_bytes];
    switch (packet->type) {
    case MQTT_PACKET_CONNACK:
        if (remaining_length != 2u || packet->flags != 0u ||
            (payload[0] & 0xfeu) != 0u) {
            return ERR_PROTOCOL;
        }
        packet->session_present = payload[0] & 1u;
        packet->return_code = payload[1];
        break;
    case MQTT_PACKET_PUBACK:
        if (remaining_length != 2u || packet->flags != 0u) {
            return ERR_PROTOCOL;
        }
        packet->packet_id = (uint16_t)((uint16_t)payload[0] << 8u) | payload[1];
        if (packet->packet_id == 0u) {
            return ERR_PROTOCOL;
        }
        break;
    case MQTT_PACKET_PINGRESP:
        if (remaining_length != 0u || packet->flags != 0u) {
            return ERR_PROTOCOL;
        }
        break;
    default:
        return ERR_UNSUPPORTED;
    }
    *consumed = total_length;
    return SYS_OK;
}
