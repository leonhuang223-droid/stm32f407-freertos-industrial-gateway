#ifndef GATEWAY_MQTT_CODEC_H
#define GATEWAY_MQTT_CODEC_H

#include "error_code.h"

#include <stddef.h>
#include <stdint.h>

#define MQTT_MAX_REMAINING_LENGTH 268435455u

typedef enum {
    MQTT_PACKET_CONNECT = 1,
    MQTT_PACKET_CONNACK = 2,
    MQTT_PACKET_PUBLISH = 3,
    MQTT_PACKET_PUBACK = 4,
    MQTT_PACKET_PINGREQ = 12,
    MQTT_PACKET_PINGRESP = 13,
    MQTT_PACKET_DISCONNECT = 14
} mqtt_packet_type_t;

typedef struct {
    const char *client_id;
    const char *username;
    const char *password;
    uint16_t keep_alive_seconds;
    uint8_t clean_session;
} mqtt_connect_options_t;

typedef struct {
    mqtt_packet_type_t type;
    uint16_t packet_id;
    uint8_t flags;
    uint8_t return_code;
    uint8_t session_present;
} mqtt_packet_view_t;

status_t mqtt_encode_connect(const mqtt_connect_options_t *options,
                             uint8_t *buffer, size_t capacity,
                             size_t *length);
status_t mqtt_encode_publish_qos1(const char *topic,
                                  const uint8_t *payload,
                                  size_t payload_length,
                                  uint16_t packet_id, uint8_t duplicate,
                                  uint8_t *buffer, size_t capacity,
                                  size_t *length);
status_t mqtt_encode_pingreq(uint8_t *buffer, size_t capacity,
                             size_t *length);
status_t mqtt_encode_disconnect(uint8_t *buffer, size_t capacity,
                                size_t *length);
status_t mqtt_decode_packet(const uint8_t *buffer, size_t length,
                            mqtt_packet_view_t *packet,
                            size_t *consumed);

#endif
