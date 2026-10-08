#ifndef GATEWAY_ALARM_SUBSYSTEM_H
#define GATEWAY_ALARM_SUBSYSTEM_H

#include "gateway_model.h"
#include "relay.h"

#include <stddef.h>
#include <stdint.h>

#define ALARM_MAX_EVENTS_PER_MEASUREMENT 3u

typedef enum {
    ALARM_STATE_NORMAL = 0,
    ALARM_STATE_PENDING,
    ALARM_STATE_ACTIVE,
    ALARM_STATE_RECOVER_PENDING
} alarm_state_t;

typedef struct {
    alarm_state_t state;
    uint8_t assert_count;
    uint8_t recover_count;
    uint8_t acknowledged;
    uint32_t last_event_id;
} alarm_condition_runtime_t;

typedef struct {
    gateway_alarm_rule_config_t config;
    alarm_condition_runtime_t high;
    alarm_condition_runtime_t low;
    alarm_condition_runtime_t quality;
    uint8_t quality_bad;
} alarm_rule_runtime_t;

typedef struct {
    uint32_t processed_measurements;
    uint32_t entered_events;
    uint32_t recovered_events;
    uint32_t relay_errors;
    uint32_t active_alarms;
    status_t last_error;
} alarm_health_t;

typedef struct {
    relay_t *relay;
    gateway_runtime_config_t config;
    alarm_rule_runtime_t rules[GATEWAY_MAX_ALARM_RULES];
    alarm_health_t health;
    uint32_t next_event_id;
    uint8_t initialized;
} alarm_subsystem_t;

status_t alarm_subsystem_construct(alarm_subsystem_t *subsystem,
                                   relay_t *relay,
                                   const gateway_runtime_config_t *config);
status_t alarm_subsystem_start(alarm_subsystem_t *subsystem);
status_t alarm_subsystem_reconfigure(alarm_subsystem_t *subsystem,
                                     const gateway_runtime_config_t *config);
status_t alarm_subsystem_process(alarm_subsystem_t *subsystem,
                                 const gateway_measurement_t *measurement,
                                 gateway_alarm_event_t *events,
                                 size_t capacity,
                                 size_t *event_count);
status_t alarm_subsystem_acknowledge(alarm_subsystem_t *subsystem,
                                     uint32_t event_id,
                                     gateway_alarm_event_t *event);
status_t alarm_subsystem_get_health(const alarm_subsystem_t *subsystem,
                                    alarm_health_t *health);
uint32_t alarm_subsystem_active_count(const alarm_subsystem_t *subsystem);

#endif
