#include "alarm_subsystem.h"

#include <limits.h>
#include <string.h>

typedef struct {
    gateway_alarm_type_t type;
    int condition_active;
    int recovery_active;
    int32_t threshold;
} condition_input_t;

static uint32_t count_active(const alarm_subsystem_t *subsystem);

static int config_valid(const gateway_runtime_config_t *config)
{
    unsigned int i;

    if (config == 0 ||
        config->schema_version != GATEWAY_RUNTIME_CONFIG_SCHEMA_VERSION ||
        config->rule_count == 0u ||
        config->rule_count > GATEWAY_MAX_ALARM_RULES) {
        return 0;
    }
    for (i = 0u; i < config->rule_count; ++i) {
        const gateway_alarm_rule_config_t *rule = &config->rules[i];
        unsigned int j;

        if (rule->point_id == 0u ||
            (rule->high_enabled == 0u && rule->low_enabled == 0u) ||
            rule->assert_samples == 0u || rule->recover_samples == 0u ||
            rule->hysteresis < 0 ||
            (rule->high_enabled != 0u && rule->low_enabled != 0u &&
             rule->low_threshold >= rule->high_threshold)) {
            return 0;
        }
        for (j = 0u; j < i; ++j) {
            if (config->rules[j].point_id == rule->point_id) {
                return 0;
            }
        }
    }
    return 1;
}

status_t alarm_subsystem_construct(alarm_subsystem_t *subsystem,
                                   relay_t *relay,
                                   const gateway_runtime_config_t *config)
{
    unsigned int i;

    if (subsystem == 0 || relay == 0 || !config_valid(config)) {
        return ERR_INVALID_ARG;
    }
    memset(subsystem, 0, sizeof(*subsystem));
    subsystem->relay = relay;
    subsystem->config = *config;
    subsystem->next_event_id = 1u;
    subsystem->health.last_error = ERR_DEVICE_NOT_READY;
    for (i = 0u; i < config->rule_count; ++i) {
        subsystem->rules[i].config = config->rules[i];
    }
    return SYS_OK;
}

status_t alarm_subsystem_start(alarm_subsystem_t *subsystem)
{
    status_t status;

    if (subsystem == 0 || subsystem->relay == 0) {
        return ERR_INVALID_ARG;
    }
    status = relay_init(subsystem->relay);
    subsystem->initialized = status == SYS_OK ? 1u : 0u;
    subsystem->health.last_error = status;
    return status;
}

status_t alarm_subsystem_reconfigure(
    alarm_subsystem_t *subsystem,
    const gateway_runtime_config_t *config)
{
    alarm_rule_runtime_t rules[GATEWAY_MAX_ALARM_RULES];
    status_t status = SYS_OK;
    unsigned int i;

    if (subsystem == 0 || !config_valid(config) || subsystem->relay == 0) {
        return ERR_INVALID_ARG;
    }
    if (count_active(subsystem) != 0u) {
        subsystem->health.last_error = ERR_DEVICE_NOT_READY;
        return ERR_DEVICE_NOT_READY;
    }
    memset(rules, 0, sizeof(rules));
    for (i = 0u; i < config->rule_count; ++i) {
        rules[i].config = config->rules[i];
    }
    if (subsystem->initialized != 0u) {
        status = relay_set(subsystem->relay, RELAY_DEENERGIZED);
    }
    if (status != SYS_OK) {
        subsystem->health.last_error = status;
        return status;
    }
    subsystem->relay->config.safe_state = config->relay_safe_energized != 0u
        ? RELAY_ENERGIZED : RELAY_DEENERGIZED;
    subsystem->config = *config;
    memcpy(subsystem->rules, rules, sizeof(rules));
    subsystem->health.active_alarms = 0u;
    subsystem->health.last_error = SYS_OK;
    return SYS_OK;
}

static int condition_is_active(const alarm_condition_runtime_t *runtime)
{
    return runtime->state == ALARM_STATE_ACTIVE ||
           runtime->state == ALARM_STATE_RECOVER_PENDING;
}

static gateway_alarm_event_t make_event(alarm_subsystem_t *subsystem,
                                        alarm_condition_runtime_t *runtime,
                                        const gateway_measurement_t *measurement,
                                        const condition_input_t *input,
                                        gateway_alarm_transition_t transition)
{
    gateway_alarm_event_t event;

    memset(&event, 0, sizeof(event));
    event.event_id = subsystem->next_event_id++;
    if (subsystem->next_event_id == 0u) {
        subsystem->next_event_id = 1u;
    }
    event.measurement_sequence = measurement->sequence;
    event.monotonic_ms = measurement->monotonic_ms;
    event.wall_time_ms = measurement->wall_time_ms;
    event.point_id = measurement->point_id;
    event.type = input->type;
    event.transition = transition;
    event.quality = measurement->quality;
    event.threshold = input->threshold;
    event.value = measurement->engineering_value;
    runtime->last_event_id = event.event_id;
    runtime->acknowledged = 0u;
    return event;
}

static status_t update_condition(
    alarm_subsystem_t *subsystem, alarm_condition_runtime_t *runtime,
    const gateway_measurement_t *measurement,
    const gateway_alarm_rule_config_t *config,
    const condition_input_t *input, gateway_alarm_event_t *events,
    size_t capacity, size_t *event_count)
{
    gateway_alarm_transition_t transition = GATEWAY_ALARM_ENTERED;
    int emit_event = 0;

    switch (runtime->state) {
    case ALARM_STATE_NORMAL:
        if (input->condition_active) {
            runtime->assert_count = 1u;
            runtime->state = config->assert_samples <= 1u
                ? ALARM_STATE_ACTIVE : ALARM_STATE_PENDING;
            emit_event = runtime->state == ALARM_STATE_ACTIVE;
        }
        break;
    case ALARM_STATE_PENDING:
        if (!input->condition_active) {
            runtime->state = ALARM_STATE_NORMAL;
            runtime->assert_count = 0u;
        } else if (runtime->assert_count < UINT8_MAX) {
            runtime->assert_count++;
            if (runtime->assert_count >= config->assert_samples) {
                runtime->state = ALARM_STATE_ACTIVE;
                emit_event = 1;
            }
        }
        break;
    case ALARM_STATE_ACTIVE:
        if (input->recovery_active) {
            runtime->recover_count = 1u;
            runtime->state = config->recover_samples <= 1u
                ? ALARM_STATE_NORMAL : ALARM_STATE_RECOVER_PENDING;
            emit_event = runtime->state == ALARM_STATE_NORMAL;
            transition = GATEWAY_ALARM_RECOVERED;
        }
        break;
    case ALARM_STATE_RECOVER_PENDING:
        if (!input->recovery_active) {
            runtime->state = ALARM_STATE_ACTIVE;
            runtime->recover_count = 0u;
        } else if (input->recovery_active &&
                   runtime->recover_count < UINT8_MAX) {
            runtime->recover_count++;
            if (runtime->recover_count >= config->recover_samples) {
                runtime->state = ALARM_STATE_NORMAL;
                runtime->assert_count = 0u;
                runtime->recover_count = 0u;
                emit_event = 1;
                transition = GATEWAY_ALARM_RECOVERED;
            }
        }
        break;
    default:
        return ERR_PROTOCOL;
    }
    if (!emit_event) {
        return SYS_OK;
    }
    if (*event_count >= capacity) {
        return ERR_NO_MEMORY;
    }
    events[*event_count] = make_event(subsystem, runtime, measurement,
                                      input, transition);
    (*event_count)++;
    if (transition == GATEWAY_ALARM_ENTERED) {
        subsystem->health.entered_events++;
    } else {
        subsystem->health.recovered_events++;
    }
    return SYS_OK;
}

static uint32_t count_active(const alarm_subsystem_t *subsystem)
{
    uint32_t count = 0u;
    unsigned int i;

    for (i = 0u; i < subsystem->config.rule_count; ++i) {
        const alarm_rule_runtime_t *rule = &subsystem->rules[i];

        count += condition_is_active(&rule->high) ? 1u : 0u;
        count += condition_is_active(&rule->low) ? 1u : 0u;
        count += condition_is_active(&rule->quality) ? 1u : 0u;
    }
    return count;
}

static status_t apply_relay_policy(alarm_subsystem_t *subsystem)
{
    int force_safe = 0;
    int alarm_requests_relay = 0;
    unsigned int i;
    relay_state_t desired;
    status_t status;

    for (i = 0u; i < subsystem->config.rule_count; ++i) {
        const alarm_rule_runtime_t *rule = &subsystem->rules[i];

        if (rule->quality_bad != 0u ||
            condition_is_active(&rule->quality)) {
            force_safe = 1;
        }
        if (rule->config.relay_on_alarm != 0u &&
            (condition_is_active(&rule->high) ||
             condition_is_active(&rule->low))) {
            alarm_requests_relay = 1;
        }
    }
    if (force_safe) {
        status = relay_force_safe(subsystem->relay);
    } else {
        desired = alarm_requests_relay ? RELAY_ENERGIZED
                                       : RELAY_DEENERGIZED;
        status = relay_set(subsystem->relay, desired);
    }
    if (status != SYS_OK) {
        subsystem->health.relay_errors++;
    }
    return status;
}

status_t alarm_subsystem_process(
    alarm_subsystem_t *subsystem, const gateway_measurement_t *measurement,
    gateway_alarm_event_t *events, size_t capacity, size_t *event_count)
{
    alarm_rule_runtime_t *rule = 0;
    condition_input_t input;
    status_t first_error = SYS_OK;
    unsigned int i;

    if (subsystem == 0 || subsystem->initialized == 0u ||
        measurement == 0 || events == 0 || capacity == 0u ||
        event_count == 0) {
        return ERR_INVALID_ARG;
    }
    *event_count = 0u;
    for (i = 0u; i < subsystem->config.rule_count; ++i) {
        if (subsystem->rules[i].config.point_id == measurement->point_id) {
            rule = &subsystem->rules[i];
            break;
        }
    }
    if (rule == 0) {
        return ERR_UNSUPPORTED;
    }
    subsystem->health.processed_measurements++;
    rule->quality_bad = measurement->quality == GATEWAY_QUALITY_GOOD
        ? 0u : 1u;

    input.type = GATEWAY_ALARM_DATA_QUALITY;
    input.condition_active = rule->quality_bad != 0u;
    input.recovery_active = rule->quality_bad == 0u;
    input.threshold = 0;
    first_error = update_condition(subsystem, &rule->quality, measurement,
                                   &rule->config, &input, events, capacity,
                                   event_count);

    if (measurement->quality == GATEWAY_QUALITY_GOOD) {
        status_t status;

        if (rule->config.high_enabled != 0u) {
            input.type = GATEWAY_ALARM_HIGH;
            input.condition_active = measurement->engineering_value >=
                                     rule->config.high_threshold;
            input.recovery_active = (int64_t)measurement->engineering_value <=
                (int64_t)rule->config.high_threshold -
                (int64_t)rule->config.hysteresis;
            input.threshold = rule->config.high_threshold;
            status = update_condition(subsystem, &rule->high, measurement,
                                      &rule->config, &input, events, capacity,
                                      event_count);
            if (first_error == SYS_OK && status != SYS_OK) {
                first_error = status;
            }
        }
        if (rule->config.low_enabled != 0u) {
            input.type = GATEWAY_ALARM_LOW;
            input.condition_active = measurement->engineering_value <=
                                     rule->config.low_threshold;
            input.recovery_active = (int64_t)measurement->engineering_value >=
                (int64_t)rule->config.low_threshold +
                (int64_t)rule->config.hysteresis;
            input.threshold = rule->config.low_threshold;
            status = update_condition(subsystem, &rule->low, measurement,
                                      &rule->config, &input, events, capacity,
                                      event_count);
            if (first_error == SYS_OK && status != SYS_OK) {
                first_error = status;
            }
        }
    } else {
        /* An invalid sample interrupts consecutive threshold observations,
         * but must never clear an already asserted alarm. */
        alarm_condition_runtime_t *conditions[] = { &rule->high, &rule->low };
        for (i = 0u; i < 2u; ++i) {
            if (conditions[i]->state == ALARM_STATE_PENDING) {
                conditions[i]->state = ALARM_STATE_NORMAL;
            } else if (conditions[i]->state == ALARM_STATE_RECOVER_PENDING) {
                conditions[i]->state = ALARM_STATE_ACTIVE;
            }
            conditions[i]->assert_count = 0u;
            conditions[i]->recover_count = 0u;
        }
    }
    subsystem->health.active_alarms = count_active(subsystem);
    {
        status_t relay_status = apply_relay_policy(subsystem);
        if (first_error == SYS_OK && relay_status != SYS_OK) {
            first_error = relay_status;
        }
    }
    subsystem->health.last_error = first_error;
    return first_error;
}

status_t alarm_subsystem_acknowledge(alarm_subsystem_t *subsystem,
                                     uint32_t event_id,
                                     gateway_alarm_event_t *event)
{
    unsigned int i;

    if (subsystem == 0 || event == 0 || event_id == 0u) {
        return ERR_INVALID_ARG;
    }
    for (i = 0u; i < subsystem->config.rule_count; ++i) {
        alarm_rule_runtime_t *rule = &subsystem->rules[i];
        alarm_condition_runtime_t *conditions[] = {
            &rule->high, &rule->low, &rule->quality
        };
        gateway_alarm_type_t types[] = {
            GATEWAY_ALARM_HIGH, GATEWAY_ALARM_LOW,
            GATEWAY_ALARM_DATA_QUALITY
        };
        unsigned int j;

        for (j = 0u; j < 3u; ++j) {
            if (conditions[j]->last_event_id == event_id &&
                condition_is_active(conditions[j])) {
                memset(event, 0, sizeof(*event));
                conditions[j]->acknowledged = 1u;
                event->event_id = subsystem->next_event_id++;
                event->point_id = rule->config.point_id;
                event->type = types[j];
                event->transition = GATEWAY_ALARM_ACKNOWLEDGED;
                event->threshold = types[j] == GATEWAY_ALARM_HIGH
                    ? rule->config.high_threshold
                    : (types[j] == GATEWAY_ALARM_LOW
                       ? rule->config.low_threshold : 0);
                return SYS_OK;
            }
        }
    }
    return ERR_DEVICE_NOT_READY;
}

status_t alarm_subsystem_get_health(const alarm_subsystem_t *subsystem,
                                    alarm_health_t *health)
{
    if (subsystem == 0 || health == 0) {
        return ERR_INVALID_ARG;
    }
    *health = subsystem->health;
    return SYS_OK;
}

uint32_t alarm_subsystem_active_count(const alarm_subsystem_t *subsystem)
{
    return subsystem != 0 ? count_active(subsystem) : 0u;
}
