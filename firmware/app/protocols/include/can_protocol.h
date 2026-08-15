#ifndef GATEWAY_CAN_PROTOCOL_H
#define GATEWAY_CAN_PROTOCOL_H

#include "can_bus.h"
#include "gateway_model.h"

#include <stdint.h>

typedef enum {
    CAN_PROTOCOL_MEASUREMENT = 1,
    CAN_PROTOCOL_STATUS = 2
} can_protocol_message_type_t;

status_t can_protocol_encode_measurement(uint8_t node_id,
                                         const gateway_measurement_t *source,
                                         can_frame_t *frame);
status_t can_protocol_decode_measurement(const can_frame_t *frame,
                                         uint32_t now_ms,
                                         uint8_t *source_node_id,
                                         gateway_measurement_t *measurement);

#endif
