#include "can_protocol.h"

#include <string.h>

#define CAN_PROTOCOL_NODE_MASK 0xffu
#define CAN_PROTOCOL_TYPE_SHIFT 8u
#define CAN_PROTOCOL_MAX_NODE_ID 127u

status_t can_protocol_encode_measurement(uint8_t node_id,
                                         const gateway_measurement_t *source,
                                         can_frame_t *frame)
{
    uint32_t value;

    if (node_id == 0u || node_id > CAN_PROTOCOL_MAX_NODE_ID || source == 0 ||
        frame == 0 || source->point_id == 0u ||
        (uint32_t)source->quality > GATEWAY_QUALITY_UNAVAILABLE) {
        return ERR_INVALID_ARG;
    }
    memset(frame, 0, sizeof(*frame));
    frame->id =
        ((uint32_t)CAN_PROTOCOL_MEASUREMENT << CAN_PROTOCOL_TYPE_SHIFT) |
        node_id;
    frame->dlc = 8u;
    frame->data[0] = (uint8_t)source->sequence;
    frame->data[1] = (uint8_t)source->quality;
    frame->data[2] = (uint8_t)(source->point_id >> 8u);
    frame->data[3] = (uint8_t)source->point_id;
    value = (uint32_t)source->engineering_value;
    frame->data[4] = (uint8_t)(value >> 24u);
    frame->data[5] = (uint8_t)(value >> 16u);
    frame->data[6] = (uint8_t)(value >> 8u);
    frame->data[7] = (uint8_t)value;
    return SYS_OK;
}

status_t can_protocol_decode_measurement(const can_frame_t *frame,
                                         uint32_t now_ms,
                                         uint8_t *source_node_id,
                                         gateway_measurement_t *measurement)
{
    uint32_t type;
    uint32_t value;

    if (frame == 0 || source_node_id == 0 || measurement == 0 ||
        frame->is_extended != 0u || frame->is_remote != 0u ||
        frame->id > 0x7ffu || frame->dlc != 8u) {
        return ERR_INVALID_ARG;
    }
    type = (frame->id >> CAN_PROTOCOL_TYPE_SHIFT) & 0x07u;
    if (type != CAN_PROTOCOL_MEASUREMENT) {
        return ERR_UNSUPPORTED;
    }
    *source_node_id = (uint8_t)(frame->id & CAN_PROTOCOL_NODE_MASK);
    if (*source_node_id == 0u || *source_node_id > CAN_PROTOCOL_MAX_NODE_ID ||
        frame->data[1] > GATEWAY_QUALITY_UNAVAILABLE) {
        return ERR_PROTOCOL;
    }
    memset(measurement, 0, sizeof(*measurement));
    measurement->point_id =
        (uint16_t)((uint16_t)frame->data[2] << 8u) | frame->data[3];
    value = ((uint32_t)frame->data[4] << 24u) |
            ((uint32_t)frame->data[5] << 16u) |
            ((uint32_t)frame->data[6] << 8u) | frame->data[7];
    measurement->source = GATEWAY_SOURCE_CAN;
    measurement->unit = GATEWAY_UNIT_RAW;
    measurement->sequence = frame->data[0];
    measurement->monotonic_ms = now_ms;
    measurement->raw_value = (int32_t)value;
    measurement->engineering_value = (int32_t)value;
    measurement->quality = (gateway_quality_t)frame->data[1];
    measurement->error =
        measurement->quality == GATEWAY_QUALITY_GOOD ? SYS_OK : ERR_IO;
    return measurement->point_id != 0u ? SYS_OK : ERR_PROTOCOL;
}
