#include "network_subsystem.h"

#include "mqtt_codec.h"

#include <stdio.h>
#include <string.h>

static int time_reached(uint32_t now, uint32_t deadline)
{
    return (int32_t)(now - deadline) >= 0;
}

static uint32_t deadline_after(uint32_t now, uint32_t delay)
{
    return now + delay;
}

static int valid_identifier(const char *text)
{
    size_t i;

    if (text == 0 || text[0] == '\0') {
        return 0;
    }
    for (i = 0u; text[i] != '\0'; ++i) {
        char value = text[i];

        if (!((value >= 'a' && value <= 'z') ||
              (value >= 'A' && value <= 'Z') ||
              (value >= '0' && value <= '9') || value == '-' || value == '_')) {
            return 0;
        }
    }
    return 1;
}

static int copy_text(char *destination,
                     size_t capacity,
                     const char *source,
                     int allow_empty)
{
    size_t length;

    if (destination == 0 || capacity == 0u || source == 0) {
        return 0;
    }
    length = strlen(source);
    if ((!allow_empty && length == 0u) || length >= capacity) {
        return 0;
    }
    memcpy(destination, source, length + 1u);
    return 1;
}

static void
set_offline(network_subsystem_t *subsystem, uint32_t now_ms, status_t reason)
{
    status_t cleanup_status = network_transport_suspend(subsystem->transport);
    subsystem->health.state = NETWORK_STATE_OFFLINE;
    subsystem->health.mqtt_ready = 0u;
    subsystem->health.disconnects++;
    subsystem->health.last_error =
        cleanup_status == SYS_OK ? reason : cleanup_status;
    subsystem->health.retry_due_ms =
        deadline_after(now_ms, subsystem->current_reconnect_ms);
    if (subsystem->current_reconnect_ms < subsystem->reconnect_max_ms) {
        uint32_t doubled = subsystem->current_reconnect_ms * 2u;

        subsystem->current_reconnect_ms =
            doubled < subsystem->current_reconnect_ms ||
                    doubled > subsystem->reconnect_max_ms
                ? subsystem->reconnect_max_ms
                : doubled;
    }
    subsystem->mqtt_rx_length = 0u;
    subsystem->connect_phase = 0u;
    subsystem->ping_outstanding = 0u;
    if (subsystem->inflight_active != 0u) {
        subsystem->inflight_duplicate = 1u;
        subsystem->inflight_retries = 0u;
    }
}

static status_t prepare_transport(network_subsystem_t *subsystem,
                                  uint32_t now_ms)
{
    status_t status;

    /* Retry a failed close before reusing a connection from the old session. */
    if (network_transport_is_connected(subsystem->transport) != 0u) {
        status = network_transport_close(subsystem->transport);
        if (status != SYS_OK) {
            return status;
        }
    }
    return network_transport_is_initialized(subsystem->transport) != 0u
               ? SYS_OK
               : network_transport_init_step(subsystem->transport, now_ms);
}

static status_t append_mqtt_rx(network_subsystem_t *subsystem,
                               const uint8_t *data,
                               size_t length)
{
    if (length > sizeof(subsystem->mqtt_rx) - subsystem->mqtt_rx_length) {
        return ERR_NO_MEMORY;
    }
    memcpy(&subsystem->mqtt_rx[subsystem->mqtt_rx_length], data, length);
    subsystem->mqtt_rx_length += length;
    return SYS_OK;
}

static status_t receive_connack(network_subsystem_t *subsystem)
{
    unsigned int attempt;
    uint32_t slice = subsystem->connect_timeout_ms / 8u;

    if (slice == 0u) {
        slice = 1u;
    }
    for (attempt = 0u; attempt < 8u; ++attempt) {
        uint8_t incoming[128];
        size_t incoming_length = 0u;
        size_t consumed = 0u;
        mqtt_packet_view_t packet;
        status_t status = network_transport_receive(subsystem->transport,
                                                    incoming,
                                                    sizeof(incoming),
                                                    &incoming_length,
                                                    slice);

        if (status != SYS_OK) {
            if (status == ERR_TIMEOUT) {
                continue;
            }
            return status;
        }
        status = append_mqtt_rx(subsystem, incoming, incoming_length);
        if (status != SYS_OK) {
            return status;
        }
        status = mqtt_decode_packet(
            subsystem->mqtt_rx, subsystem->mqtt_rx_length, &packet, &consumed);
        if (status == ERR_DEVICE_NOT_READY) {
            continue;
        }
        if (status != SYS_OK || packet.type != MQTT_PACKET_CONNACK ||
            packet.return_code != 0u) {
            return ERR_PROTOCOL;
        }
        memmove(subsystem->mqtt_rx,
                &subsystem->mqtt_rx[consumed],
                subsystem->mqtt_rx_length - consumed);
        subsystem->mqtt_rx_length -= consumed;
        return SYS_OK;
    }
    return ERR_TIMEOUT;
}

static status_t send_mqtt_connect(network_subsystem_t *subsystem,
                                  uint32_t now_ms)
{
    status_t status;

    mqtt_connect_options_t options;
    uint8_t packet[NETWORK_MQTT_PACKET_MAX];
    size_t length = 0u;
    memset(&options, 0, sizeof(options));
    options.client_id = subsystem->client_id;
    options.username = subsystem->username[0] != '\0' ? subsystem->username : 0;
    options.password = subsystem->password[0] != '\0' ? subsystem->password : 0;
    options.keep_alive_seconds = subsystem->keep_alive_seconds;
    options.clean_session = 1u;
    status = mqtt_encode_connect(&options, packet, sizeof(packet), &length);
    if (status == SYS_OK) {
        status = network_transport_send(subsystem->transport, packet, length);
    }
    if (status == SYS_OK) {
        subsystem->mqtt_rx_length = 0u;
        subsystem->connect_deadline_ms = now_ms + subsystem->connect_timeout_ms;
        subsystem->connect_phase = 4u;
        return ERR_IN_PROGRESS;
    }
    return status;
}

static status_t wait_mqtt_connack(network_subsystem_t *subsystem,
                                  uint32_t now_ms)
{
    status_t status;

    uint8_t incoming[128];
    size_t length = 0u;
    size_t consumed = 0u;
    mqtt_packet_view_t packet;
    status = network_transport_receive(
        subsystem->transport, incoming, sizeof(incoming), &length, 10u);
    if (status == SYS_OK) {
        status = append_mqtt_rx(subsystem, incoming, length);
        if (status == SYS_OK) {
            status = mqtt_decode_packet(subsystem->mqtt_rx,
                                        subsystem->mqtt_rx_length,
                                        &packet,
                                        &consumed);
        }
        if (status == SYS_OK) {
            if (packet.type != MQTT_PACKET_CONNACK ||
                packet.return_code != 0u) {
                status = ERR_PROTOCOL;
                return status;
            }
            memmove(subsystem->mqtt_rx,
                    subsystem->mqtt_rx + consumed,
                    subsystem->mqtt_rx_length - consumed);
            subsystem->mqtt_rx_length -= consumed;
            subsystem->connect_phase = 0u;
            subsystem->health.state = NETWORK_STATE_MQTT_READY;
            subsystem->health.mqtt_ready = 1u;
            subsystem->health.successful_connections++;
            subsystem->health.last_error = SYS_OK;
            subsystem->health.retry_due_ms = 0u;
            subsystem->current_reconnect_ms = subsystem->reconnect_initial_ms;
            subsystem->last_activity_ms = now_ms;
            return SYS_OK;
        }
    }
    if (status == ERR_TIMEOUT || status == ERR_DEVICE_NOT_READY) {
        if (!time_reached(now_ms, subsystem->connect_deadline_ms)) {
            return ERR_IN_PROGRESS;
        }
        status = ERR_TIMEOUT;
    }
    return status;
}

static status_t connect_mqtt_step(network_subsystem_t *subsystem,
                                  uint32_t now_ms)
{
    status_t status;
    subsystem->health.state = NETWORK_STATE_CONNECTING;
    if (subsystem->connect_phase == 0u) {
        subsystem->health.connect_attempts++;
        subsystem->connect_phase = 1u;
    }
    switch (subsystem->connect_phase) {
    case 1u:
        status = prepare_transport(subsystem, now_ms);
        if (status == SYS_OK) {
            subsystem->connect_phase = 2u;
            return ERR_IN_PROGRESS;
        }
        break;
    case 2u:
        status = network_transport_connect_step(subsystem->transport,
                                                subsystem->broker_host,
                                                subsystem->broker_port,
                                                now_ms);
        if (status == SYS_OK) {
            subsystem->connect_phase = 3u;
            return ERR_IN_PROGRESS;
        }
        break;
    case 3u:
        status = send_mqtt_connect(subsystem, now_ms);
        break;
    case 4u:
        status = wait_mqtt_connack(subsystem, now_ms);
        break;
    default:
        status = ERR_PROTOCOL;
        break;
    }
    if (status != SYS_OK && status != ERR_IN_PROGRESS) {
        set_offline(subsystem, now_ms, status);
    }
    return status;
}

static status_t connect_mqtt(network_subsystem_t *subsystem, uint32_t now_ms)
{
    mqtt_connect_options_t options;
    uint8_t packet[NETWORK_MQTT_PACKET_MAX];
    size_t packet_length = 0u;
    status_t status;

    if (network_transport_has_connect_steps(subsystem->transport) != 0u) {
        return connect_mqtt_step(subsystem, now_ms);
    }
    subsystem->health.connect_attempts++;
    status = prepare_transport(subsystem, now_ms);
    if (status != SYS_OK) {
        set_offline(subsystem, now_ms, status);
        return status;
    }
    status = network_transport_connect(
        subsystem->transport, subsystem->broker_host, subsystem->broker_port);
    if (status != SYS_OK) {
        set_offline(subsystem, now_ms, status);
        return status;
    }
    memset(&options, 0, sizeof(options));
    options.client_id = subsystem->client_id;
    options.username = subsystem->username[0] != '\0' ? subsystem->username : 0;
    options.password = subsystem->password[0] != '\0' ? subsystem->password : 0;
    options.keep_alive_seconds = subsystem->keep_alive_seconds;
    options.clean_session = 1u;
    status =
        mqtt_encode_connect(&options, packet, sizeof(packet), &packet_length);
    if (status == SYS_OK) {
        status =
            network_transport_send(subsystem->transport, packet, packet_length);
    }
    if (status == SYS_OK) {
        status = receive_connack(subsystem);
    }
    if (status != SYS_OK) {
        set_offline(subsystem, now_ms, status);
        return status;
    }
    subsystem->health.state = NETWORK_STATE_MQTT_READY;
    subsystem->health.mqtt_ready = 1u;
    subsystem->health.successful_connections++;
    subsystem->health.last_error = SYS_OK;
    subsystem->health.retry_due_ms = 0u;
    subsystem->current_reconnect_ms = subsystem->reconnect_initial_ms;
    subsystem->last_activity_ms = now_ms;
    return SYS_OK;
}

static uint16_t allocate_packet_id(network_subsystem_t *subsystem)
{
    uint16_t packet_id = subsystem->next_packet_id++;

    if (packet_id == 0u) {
        packet_id = subsystem->next_packet_id++;
    }
    if (subsystem->next_packet_id == 0u) {
        subsystem->next_packet_id = 1u;
    }
    return packet_id;
}

/** Synchronous request; pointed-to buffers remain caller-owned.
 * @author 兆鸣嵌入式
 */
typedef struct {
    size_t topic_capacity;
    uint8_t *payload;
    size_t payload_capacity;
    size_t *payload_length;
} network_payload_output_t;

static status_t build_event_payload(const network_subsystem_t *subsystem,
                                    const gateway_network_event_t *event,
                                    char *topic,
                                    const network_payload_output_t *parameters)
{
    if (parameters == 0) {
        return ERR_INVALID_ARG;
    }
    size_t topic_capacity = parameters->topic_capacity;
    uint8_t *payload = parameters->payload;
    size_t payload_capacity = parameters->payload_capacity;
    size_t *payload_length = parameters->payload_length;

    int topic_written;
    int payload_written;

    if (event->type == GATEWAY_NETWORK_ALARM) {
        const gateway_alarm_event_t *alarm = &event->payload.alarm;

        topic_written = snprintf(
            topic, topic_capacity, "factory/%s/alarm", subsystem->device_id);
        payload_written =
            snprintf((char *)payload,
                     payload_capacity,
                     "{\"device_id\":\"%s\",\"boot_id\":%lu,\"event_id\":%lu,"
                     "\"point_id\":%u,\"type\":%u,\"transition\":%u,"
                     "\"value\":%ld,\"threshold\":%ld,\"quality\":%u}",
                     subsystem->device_id,
                     (unsigned long)subsystem->boot_id,
                     (unsigned long)alarm->event_id,
                     (unsigned int)alarm->point_id,
                     (unsigned int)alarm->type,
                     (unsigned int)alarm->transition,
                     (long)alarm->value,
                     (long)alarm->threshold,
                     (unsigned int)alarm->quality);
    } else if (event->type == GATEWAY_NETWORK_TELEMETRY) {
        const gateway_measurement_t *measurement = &event->payload.measurement;

        topic_written = snprintf(topic,
                                 topic_capacity,
                                 "factory/%s/telemetry",
                                 subsystem->device_id);
        payload_written =
            snprintf((char *)payload,
                     payload_capacity,
                     "{\"device_id\":\"%s\",\"boot_id\":%lu,\"sequence\":%lu,"
                     "\"point_id\":%u,\"value\":%ld,\"unit\":%u,"
                     "\"quality\":%u}",
                     subsystem->device_id,
                     (unsigned long)subsystem->boot_id,
                     (unsigned long)event->sequence,
                     (unsigned int)measurement->point_id,
                     (long)measurement->engineering_value,
                     (unsigned int)measurement->unit,
                     (unsigned int)measurement->quality);
    } else {
        return ERR_UNSUPPORTED;
    }
    if (topic_written < 0 || (size_t)topic_written >= topic_capacity ||
        payload_written < 0 || (size_t)payload_written >= payload_capacity) {
        return ERR_NO_MEMORY;
    }
    *payload_length = (size_t)payload_written;
    return SYS_OK;
}

static status_t send_inflight(network_subsystem_t *subsystem, uint32_t now_ms)
{
    char topic[NETWORK_TOPIC_MAX];
    uint8_t payload[NETWORK_PAYLOAD_MAX];
    uint8_t packet[NETWORK_MQTT_PACKET_MAX];
    size_t payload_length = 0u;
    size_t packet_length = 0u;
    status_t status = build_event_payload(
        subsystem,
        &subsystem->inflight_event,
        topic,
        &(const network_payload_output_t){
            sizeof(topic), payload, sizeof(payload), &payload_length});

    if (status == SYS_OK) {
        status = mqtt_encode_publish_qos1(
            topic,
            &(const mqtt_publish_request_t){payload,
                                            payload_length,
                                            subsystem->inflight_packet_id,
                                            subsystem->inflight_duplicate,
                                            packet,
                                            sizeof(packet),
                                            &packet_length});
    }
    if (status == SYS_OK) {
        status =
            network_transport_send(subsystem->transport, packet, packet_length);
    }
    if (status != SYS_OK) {
        set_offline(subsystem, now_ms, status);
        return status;
    }
    subsystem->inflight_deadline_ms =
        deadline_after(now_ms, subsystem->puback_timeout_ms);
    subsystem->last_activity_ms = now_ms;
    return SYS_OK;
}

static status_t begin_next_publish(network_subsystem_t *subsystem,
                                   uint32_t now_ms)
{
    if (subsystem->inflight_active != 0u) {
        return SYS_OK;
    }
    if (subsystem->alarm_count != 0u) {
        subsystem->inflight_event =
            subsystem->alarm_backlog[subsystem->alarm_head];
        subsystem->inflight_alarm = 1u;
    } else if (subsystem->telemetry_pending != 0u) {
        subsystem->inflight_event = subsystem->latest_telemetry;
        subsystem->inflight_alarm = 0u;
        subsystem->telemetry_pending = 0u;
    } else {
        return SYS_OK;
    }
    subsystem->inflight_packet_id = allocate_packet_id(subsystem);
    subsystem->inflight_active = 1u;
    subsystem->inflight_duplicate = 0u;
    subsystem->inflight_retries = 0u;
    subsystem->health.inflight_active = 1u;
    return send_inflight(subsystem, now_ms);
}

static void complete_inflight(network_subsystem_t *subsystem)
{
    if (subsystem->inflight_alarm != 0u) {
        subsystem->alarm_head =
            (uint8_t)((subsystem->alarm_head + 1u) % NETWORK_ALARM_BACKLOG);
        subsystem->alarm_count--;
        subsystem->health.alarms_published++;
    } else {
        subsystem->health.telemetry_published++;
    }
    subsystem->inflight_active = 0u;
    subsystem->inflight_duplicate = 0u;
    subsystem->inflight_retries = 0u;
    subsystem->health.inflight_active = 0u;
    subsystem->health.alarm_backlog_count = subsystem->alarm_count;
}

static status_t handle_mqtt_packets(network_subsystem_t *subsystem,
                                    uint32_t now_ms)
{
    uint8_t incoming[128];
    size_t incoming_length = 0u;
    status_t status = network_transport_receive(
        subsystem->transport, incoming, sizeof(incoming), &incoming_length, 1u);

    if (status != SYS_OK && status != ERR_TIMEOUT) {
        set_offline(subsystem, now_ms, status);
        return status;
    }
    if (status == SYS_OK) {
        status = append_mqtt_rx(subsystem, incoming, incoming_length);
        if (status != SYS_OK) {
            subsystem->health.protocol_errors++;
            set_offline(subsystem, now_ms, status);
            return status;
        }
        subsystem->last_activity_ms = now_ms;
    }
    while (subsystem->mqtt_rx_length != 0u) {
        mqtt_packet_view_t packet;
        size_t consumed = 0u;

        status = mqtt_decode_packet(
            subsystem->mqtt_rx, subsystem->mqtt_rx_length, &packet, &consumed);
        if (status == ERR_DEVICE_NOT_READY) {
            return SYS_OK;
        }
        if (status != SYS_OK) {
            subsystem->health.protocol_errors++;
            set_offline(subsystem, now_ms, status);
            return status;
        }
        if (packet.type == MQTT_PACKET_PUBACK) {
            if (subsystem->inflight_active == 0u ||
                packet.packet_id != subsystem->inflight_packet_id) {
                subsystem->health.protocol_errors++;
                set_offline(subsystem, now_ms, ERR_PROTOCOL);
                return ERR_PROTOCOL;
            }
            subsystem->health.pubacks++;
            complete_inflight(subsystem);
        } else if (packet.type == MQTT_PACKET_PINGRESP) {
            subsystem->ping_outstanding = 0u;
        }
        memmove(subsystem->mqtt_rx,
                &subsystem->mqtt_rx[consumed],
                subsystem->mqtt_rx_length - consumed);
        subsystem->mqtt_rx_length -= consumed;
    }
    return SYS_OK;
}

static status_t maintain_keep_alive(network_subsystem_t *subsystem,
                                    uint32_t now_ms)
{
    uint32_t idle_limit = (uint32_t)subsystem->keep_alive_seconds * 500u;

    if (subsystem->ping_outstanding != 0u) {
        if (time_reached(now_ms, subsystem->ping_deadline_ms)) {
            set_offline(subsystem, now_ms, ERR_TIMEOUT);
            return ERR_TIMEOUT;
        }
        return SYS_OK;
    }
    if (time_reached(now_ms,
                     deadline_after(subsystem->last_activity_ms, idle_limit))) {
        uint8_t packet[2];
        size_t length = 0u;
        status_t status = mqtt_encode_pingreq(packet, sizeof(packet), &length);

        if (status == SYS_OK) {
            status =
                network_transport_send(subsystem->transport, packet, length);
        }
        if (status != SYS_OK) {
            set_offline(subsystem, now_ms, status);
            return status;
        }
        subsystem->ping_outstanding = 1u;
        subsystem->ping_deadline_ms =
            deadline_after(now_ms, subsystem->puback_timeout_ms);
        subsystem->last_activity_ms = now_ms;
        subsystem->health.ping_requests++;
    }
    return SYS_OK;
}

status_t network_subsystem_construct(network_subsystem_t *subsystem,
                                     network_transport_t *transport,
                                     const network_subsystem_config_t *config)
{
    if (subsystem == 0 || transport == 0 || config == 0 ||
        !valid_identifier(config->device_id) ||
        !valid_identifier(config->client_id) || config->username == 0 ||
        config->password == 0 || config->broker_host == 0 ||
        config->broker_port == 0u || config->keep_alive_seconds == 0u ||
        config->connect_timeout_ms == 0u || config->puback_timeout_ms == 0u ||
        config->connect_timeout_ms > INT32_MAX ||
        config->puback_timeout_ms > INT32_MAX ||
        config->reconnect_initial_ms == 0u ||
        config->reconnect_max_ms < config->reconnect_initial_ms ||
        config->reconnect_max_ms > INT32_MAX ||
        config->publish_retry_limit == 0u) {
        return ERR_INVALID_ARG;
    }
    memset(subsystem, 0, sizeof(*subsystem));
    if (!copy_text(subsystem->device_id,
                   sizeof(subsystem->device_id),
                   config->device_id,
                   0) ||
        !copy_text(subsystem->client_id,
                   sizeof(subsystem->client_id),
                   config->client_id,
                   0) ||
        !copy_text(subsystem->username,
                   sizeof(subsystem->username),
                   config->username,
                   1) ||
        !copy_text(subsystem->password,
                   sizeof(subsystem->password),
                   config->password,
                   1) ||
        !copy_text(subsystem->broker_host,
                   sizeof(subsystem->broker_host),
                   config->broker_host,
                   0)) {
        return ERR_INVALID_ARG;
    }
    subsystem->transport = transport;
    subsystem->broker_port = config->broker_port;
    subsystem->keep_alive_seconds = config->keep_alive_seconds;
    subsystem->boot_id = config->boot_id;
    subsystem->connect_timeout_ms = config->connect_timeout_ms;
    subsystem->puback_timeout_ms = config->puback_timeout_ms;
    subsystem->reconnect_initial_ms = config->reconnect_initial_ms;
    subsystem->reconnect_max_ms = config->reconnect_max_ms;
    subsystem->current_reconnect_ms = config->reconnect_initial_ms;
    subsystem->publish_retry_limit = config->publish_retry_limit;
    subsystem->next_packet_id = 1u;
    subsystem->health.state = NETWORK_STATE_STOPPED;
    subsystem->health.last_error = ERR_DEVICE_NOT_READY;
    return SYS_OK;
}

status_t network_subsystem_start(network_subsystem_t *subsystem,
                                 uint32_t now_ms)
{
    if (subsystem == 0 || subsystem->transport == 0) {
        return ERR_INVALID_ARG;
    }
    subsystem->health.state = NETWORK_STATE_OFFLINE;
    subsystem->health.retry_due_ms = now_ms;
    /* Start means accepted; hardware connection advances in process(). */
    return network_transport_has_connect_steps(subsystem->transport) != 0u
               ? SYS_OK
               : connect_mqtt(subsystem, now_ms);
}

status_t network_subsystem_submit(network_subsystem_t *subsystem,
                                  const gateway_network_event_t *event)
{
    uint8_t tail;

    if (subsystem == 0 || event == 0 || event->qos != 1u) {
        return ERR_INVALID_ARG;
    }
    if (event->type == GATEWAY_NETWORK_TELEMETRY) {
        subsystem->latest_telemetry = *event;
        subsystem->telemetry_pending = 1u;
        return SYS_OK;
    }
    if (event->type != GATEWAY_NETWORK_ALARM) {
        return ERR_UNSUPPORTED;
    }
    if (subsystem->alarm_count >= NETWORK_ALARM_BACKLOG) {
        subsystem->health.alarm_backlog_drops++;
        return ERR_QUEUE_FULL;
    }
    tail = (uint8_t)((subsystem->alarm_head + subsystem->alarm_count) %
                     NETWORK_ALARM_BACKLOG);
    subsystem->alarm_backlog[tail] = *event;
    subsystem->alarm_count++;
    subsystem->health.alarm_backlog_count = subsystem->alarm_count;
    return SYS_OK;
}

status_t network_subsystem_process(network_subsystem_t *subsystem,
                                   uint32_t now_ms)
{
    status_t status;

    if (subsystem == 0 || subsystem->transport == 0) {
        return ERR_INVALID_ARG;
    }
    if (subsystem->health.state == NETWORK_STATE_STOPPED) {
        return ERR_DEVICE_NOT_READY;
    }
    if (subsystem->health.ota_lease_active != 0u) {
        return SYS_OK;
    }
    if (subsystem->health.mqtt_ready == 0u) {
        if (!time_reached(now_ms, subsystem->health.retry_due_ms)) {
            return ERR_DEVICE_NOT_READY;
        }
        status = connect_mqtt(subsystem, now_ms);
        if (status != SYS_OK) {
            return status;
        }
        if (subsystem->inflight_active != 0u) {
            status = send_inflight(subsystem, now_ms);
            if (status != SYS_OK) {
                return status;
            }
        }
    }
    status = handle_mqtt_packets(subsystem, now_ms);
    if (status != SYS_OK) {
        return status;
    }
    if (subsystem->inflight_active != 0u &&
        time_reached(now_ms, subsystem->inflight_deadline_ms)) {
        subsystem->health.puback_timeouts++;
        if (subsystem->inflight_retries >= subsystem->publish_retry_limit) {
            set_offline(subsystem, now_ms, ERR_TIMEOUT);
            return ERR_TIMEOUT;
        }
        subsystem->inflight_retries++;
        subsystem->inflight_duplicate = 1u;
        subsystem->health.publish_retries++;
        status = send_inflight(subsystem, now_ms);
        if (status != SYS_OK) {
            return status;
        }
    }
    status = begin_next_publish(subsystem, now_ms);
    if (status != SYS_OK) {
        return status;
    }
    status = maintain_keep_alive(subsystem, now_ms);
    subsystem->health.last_error = status;
    return status;
}

status_t network_subsystem_suspend(network_subsystem_t *subsystem,
                                   uint32_t now_ms)
{
    uint8_t packet[2];
    size_t length = 0u;
    status_t status;

    if (subsystem == 0 || subsystem->transport == 0) {
        return ERR_INVALID_ARG;
    }
    if (subsystem->health.ota_lease_active != 0u) {
        return ERR_DEVICE_NOT_READY;
    }
    if (subsystem->health.state == NETWORK_STATE_STOPPED) {
        return SYS_OK;
    }
    if (subsystem->health.mqtt_ready != 0u &&
        mqtt_encode_disconnect(packet, sizeof(packet), &length) == SYS_OK) {
        (void)network_transport_send(subsystem->transport, packet, length);
    }
    status = network_transport_suspend(subsystem->transport);
    subsystem->connect_phase = 0u;
    if (status == SYS_OK) {
        subsystem->health.state = NETWORK_STATE_STOPPED;
        subsystem->health.mqtt_ready = 0u;
        subsystem->health.retry_due_ms = now_ms;
        subsystem->ping_outstanding = 0u;
        if (subsystem->inflight_active != 0u) {
            subsystem->inflight_duplicate = 1u;
            subsystem->inflight_retries = 0u;
        }
    }
    subsystem->health.last_error = status;
    return status;
}

status_t network_subsystem_resume(network_subsystem_t *subsystem,
                                  uint32_t now_ms)
{
    status_t status;

    if (subsystem == 0 || subsystem->transport == 0) {
        return ERR_INVALID_ARG;
    }
    if (subsystem->health.state != NETWORK_STATE_STOPPED) {
        return SYS_OK;
    }
    status = network_transport_has_connect_steps(subsystem->transport) != 0u
                 ? SYS_OK
                 : network_transport_resume(subsystem->transport);
    if (status == SYS_OK) {
        subsystem->health.state = NETWORK_STATE_OFFLINE;
        subsystem->health.retry_due_ms = now_ms;
        subsystem->health.mqtt_ready = 0u;
        subsystem->current_reconnect_ms = subsystem->reconnect_initial_ms;
    }
    subsystem->health.last_error = status;
    return status;
}

status_t network_subsystem_acquire_ota_lease(network_subsystem_t *subsystem,
                                             uint32_t now_ms)
{
    uint8_t packet[2];
    size_t length = 0u;
    status_t status;

    if (subsystem == 0 || subsystem->transport == 0) {
        return ERR_INVALID_ARG;
    }
    if (subsystem->health.ota_lease_active != 0u) {
        return SYS_OK;
    }
    if (subsystem->health.mqtt_ready != 0u &&
        mqtt_encode_disconnect(packet, sizeof(packet), &length) == SYS_OK) {
        (void)network_transport_send(subsystem->transport, packet, length);
    }
    network_transport_cancel_connect(subsystem->transport);
    subsystem->connect_phase = 0u;
    status = network_transport_close(subsystem->transport);
    if (status != SYS_OK) {
        set_offline(subsystem, now_ms, status);
        return status;
    }
    subsystem->health.state = NETWORK_STATE_OTA_LEASED;
    subsystem->health.mqtt_ready = 0u;
    subsystem->health.ota_lease_active = 1u;
    subsystem->health.ota_leases++;
    subsystem->health.retry_due_ms = now_ms;
    subsystem->ping_outstanding = 0u;
    if (subsystem->inflight_active != 0u) {
        subsystem->inflight_duplicate = 1u;
        subsystem->inflight_retries = 0u;
    }
    return SYS_OK;
}

status_t network_subsystem_release_ota_lease(network_subsystem_t *subsystem,
                                             uint32_t now_ms)
{
    status_t status;
    if (subsystem == 0 || subsystem->health.ota_lease_active == 0u) {
        return ERR_DEVICE_NOT_READY;
    }
    status = network_transport_close(subsystem->transport);
    if (status != SYS_OK) {
        subsystem->health.last_error = status;
        return status;
    }
    subsystem->health.ota_lease_active = 0u;
    subsystem->health.state = NETWORK_STATE_OFFLINE;
    subsystem->health.retry_due_ms = now_ms;
    subsystem->health.last_error = ERR_DEVICE_NOT_READY;
    return SYS_OK;
}

status_t network_subsystem_get_health(const network_subsystem_t *subsystem,
                                      network_health_t *health)
{
    if (subsystem == 0 || health == 0) {
        return ERR_INVALID_ARG;
    }
    *health = subsystem->health;
    return SYS_OK;
}
