#ifndef GATEWAY_NETWORK_SUBSYSTEM_H
#define GATEWAY_NETWORK_SUBSYSTEM_H

#include "gateway_model.h"
#include "network_transport.h"

#include <stddef.h>
#include <stdint.h>

#define NETWORK_DEVICE_ID_MAX 32u
#define NETWORK_CLIENT_ID_MAX 48u
#define NETWORK_CREDENTIAL_MAX 64u
#define NETWORK_HOST_MAX 64u
#define NETWORK_TOPIC_MAX 96u
#define NETWORK_PAYLOAD_MAX 256u
#define NETWORK_MQTT_PACKET_MAX 512u
#define NETWORK_ALARM_BACKLOG 8u

typedef enum {
    NETWORK_STATE_STOPPED = 0,
    NETWORK_STATE_OFFLINE,
    NETWORK_STATE_MQTT_READY,
    NETWORK_STATE_OTA_LEASED
} network_state_t;

typedef struct {
    const char *device_id;
    const char *client_id;
    const char *username;
    const char *password;
    const char *broker_host;
    uint16_t broker_port;
    uint16_t keep_alive_seconds;
    uint32_t boot_id;
    uint32_t connect_timeout_ms;
    uint32_t puback_timeout_ms;
    uint32_t reconnect_initial_ms;
    uint32_t reconnect_max_ms;
    uint8_t publish_retry_limit;
} network_subsystem_config_t;

typedef struct {
    network_state_t state;
    uint32_t connect_attempts;
    uint32_t successful_connections;
    uint32_t disconnects;
    uint32_t telemetry_published;
    uint32_t alarms_published;
    uint32_t pubacks;
    uint32_t publish_retries;
    uint32_t puback_timeouts;
    uint32_t ping_requests;
    uint32_t protocol_errors;
    uint32_t alarm_backlog_drops;
    uint32_t ota_leases;
    uint32_t retry_due_ms;
    status_t last_error;
    uint8_t mqtt_ready;
    uint8_t ota_lease_active;
    uint8_t inflight_active;
    uint8_t alarm_backlog_count;
} network_health_t;

typedef struct {
    network_transport_t *transport;
    char device_id[NETWORK_DEVICE_ID_MAX + 1u];
    char client_id[NETWORK_CLIENT_ID_MAX + 1u];
    char username[NETWORK_CREDENTIAL_MAX + 1u];
    char password[NETWORK_CREDENTIAL_MAX + 1u];
    char broker_host[NETWORK_HOST_MAX + 1u];
    uint16_t broker_port;
    uint16_t keep_alive_seconds;
    uint32_t boot_id;
    uint32_t connect_timeout_ms;
    uint32_t puback_timeout_ms;
    uint32_t reconnect_initial_ms;
    uint32_t reconnect_max_ms;
    uint32_t current_reconnect_ms;
    uint8_t publish_retry_limit;
    gateway_network_event_t alarm_backlog[NETWORK_ALARM_BACKLOG];
    uint8_t alarm_head;
    uint8_t alarm_count;
    gateway_network_event_t latest_telemetry;
    uint8_t telemetry_pending;
    gateway_network_event_t inflight_event;
    uint16_t inflight_packet_id;
    uint8_t inflight_active;
    uint8_t inflight_alarm;
    uint8_t inflight_duplicate;
    uint8_t inflight_retries;
    uint32_t inflight_deadline_ms;
    uint16_t next_packet_id;
    uint8_t mqtt_rx[NETWORK_MQTT_PACKET_MAX];
    size_t mqtt_rx_length;
    uint32_t last_activity_ms;
    uint32_t ping_deadline_ms;
    uint8_t ping_outstanding;
    network_health_t health;
} network_subsystem_t;

status_t network_subsystem_construct(
    network_subsystem_t *subsystem, network_transport_t *transport,
    const network_subsystem_config_t *config);
status_t network_subsystem_start(network_subsystem_t *subsystem,
                                 uint32_t now_ms);
status_t network_subsystem_submit(network_subsystem_t *subsystem,
                                  const gateway_network_event_t *event);
status_t network_subsystem_process(network_subsystem_t *subsystem,
                                   uint32_t now_ms);
status_t network_subsystem_suspend(network_subsystem_t *subsystem,
                                   uint32_t now_ms);
status_t network_subsystem_resume(network_subsystem_t *subsystem,
                                  uint32_t now_ms);
status_t network_subsystem_acquire_ota_lease(network_subsystem_t *subsystem,
                                             uint32_t now_ms);
status_t network_subsystem_release_ota_lease(network_subsystem_t *subsystem,
                                             uint32_t now_ms);
status_t network_subsystem_get_health(const network_subsystem_t *subsystem,
                                      network_health_t *health);

#endif
