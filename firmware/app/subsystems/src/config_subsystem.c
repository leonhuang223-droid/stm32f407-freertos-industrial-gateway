#include "config_subsystem.h"

#include <limits.h>
#include <string.h>

status_t config_subsystem_validate(const gateway_runtime_config_t *config)
{
    unsigned int i;

    if (config == 0 ||
        config->schema_version != GATEWAY_RUNTIME_CONFIG_SCHEMA_VERSION ||
        config->rule_count == 0u ||
        config->rule_count > GATEWAY_MAX_ALARM_RULES ||
        config->relay_safe_energized > 1u) {
        return ERR_INVALID_ARG;
    }
    for (i = 0u; i < config->rule_count; ++i) {
        const gateway_alarm_rule_config_t *rule = &config->rules[i];
        unsigned int j;

        if (rule->point_id == 0u ||
            (rule->high_enabled == 0u && rule->low_enabled == 0u) ||
            rule->high_enabled > 1u || rule->low_enabled > 1u ||
            rule->relay_on_alarm > 1u || rule->assert_samples == 0u ||
            rule->recover_samples == 0u || rule->hysteresis < 0 ||
            (rule->high_enabled != 0u && rule->low_enabled != 0u &&
             rule->low_threshold >= rule->high_threshold)) {
            return ERR_INVALID_ARG;
        }
        for (j = 0u; j < i; ++j) {
            if (config->rules[j].point_id == rule->point_id) {
                return ERR_INVALID_ARG;
            }
        }
    }
    return SYS_OK;
}

status_t config_subsystem_construct(config_subsystem_t *subsystem,
                                    const gateway_runtime_config_t *config)
{
    status_t status = config_subsystem_validate(config);

    if (subsystem == 0 || status != SYS_OK) {
        return ERR_INVALID_ARG;
    }
    memset(subsystem, 0, sizeof(*subsystem));
    subsystem->active = *config;
    subsystem->staged = *config;
    subsystem->next_request_id = 1u;
    subsystem->health.active_revision = config->revision;
    subsystem->health.last_status = SYS_OK;
    subsystem->initialized = 1u;
    return SYS_OK;
}

static gateway_alarm_rule_config_t *find_rule(
    gateway_runtime_config_t *config, uint16_t point_id)
{
    unsigned int i;

    for (i = 0u; i < config->rule_count; ++i) {
        if (config->rules[i].point_id == point_id) {
            return &config->rules[i];
        }
    }
    return 0;
}

static status_t apply_patch(gateway_runtime_config_t *config,
                            const config_patch_t *patch)
{
    gateway_alarm_rule_config_t *rule;

    if (patch->field == CONFIG_FIELD_RELAY_SAFE_ENERGIZED) {
        if (patch->point_id != 0u || patch->value < 0 || patch->value > 1) {
            return ERR_INVALID_ARG;
        }
        config->relay_safe_energized = (uint8_t)patch->value;
        return SYS_OK;
    }
    rule = find_rule(config, patch->point_id);
    if (rule == 0) {
        return ERR_UNSUPPORTED;
    }
    switch (patch->field) {
    case CONFIG_FIELD_HIGH_THRESHOLD:
        rule->high_threshold = patch->value;
        break;
    case CONFIG_FIELD_LOW_THRESHOLD:
        rule->low_threshold = patch->value;
        break;
    case CONFIG_FIELD_HYSTERESIS:
        rule->hysteresis = patch->value;
        break;
    case CONFIG_FIELD_ASSERT_SAMPLES:
        if (patch->value <= 0 || patch->value > UINT8_MAX) {
            return ERR_INVALID_ARG;
        }
        rule->assert_samples = (uint8_t)patch->value;
        break;
    case CONFIG_FIELD_RECOVER_SAMPLES:
        if (patch->value <= 0 || patch->value > UINT8_MAX) {
            return ERR_INVALID_ARG;
        }
        rule->recover_samples = (uint8_t)patch->value;
        break;
    case CONFIG_FIELD_HIGH_ENABLED:
        if (patch->value < 0 || patch->value > 1) {
            return ERR_INVALID_ARG;
        }
        rule->high_enabled = (uint8_t)patch->value;
        break;
    case CONFIG_FIELD_LOW_ENABLED:
        if (patch->value < 0 || patch->value > 1) {
            return ERR_INVALID_ARG;
        }
        rule->low_enabled = (uint8_t)patch->value;
        break;
    case CONFIG_FIELD_RELAY_ON_ALARM:
        if (patch->value < 0 || patch->value > 1) {
            return ERR_INVALID_ARG;
        }
        rule->relay_on_alarm = (uint8_t)patch->value;
        break;
    default:
        return ERR_UNSUPPORTED;
    }
    return SYS_OK;
}

status_t config_subsystem_prepare(config_subsystem_t *subsystem,
                                  const config_patch_t *patch,
                                  gateway_storage_config_request_t *request)
{
    gateway_runtime_config_t candidate;
    status_t status;

    if (subsystem == 0 || patch == 0 || request == 0 ||
        subsystem->initialized == 0u) {
        return ERR_INVALID_ARG;
    }
    candidate = subsystem->staged;
    status = apply_patch(&candidate, patch);
    if (status == SYS_OK) {
        if (candidate.revision == UINT32_MAX) {
            status = ERR_UNSUPPORTED;
        } else {
            candidate.revision++;
            status = config_subsystem_validate(&candidate);
        }
    }
    if (status != SYS_OK) {
        subsystem->health.rejected_requests++;
        subsystem->health.last_status = status;
        return status;
    }
    request->request_id = subsystem->next_request_id++;
    if (subsystem->next_request_id == 0u) {
        subsystem->next_request_id = 1u;
    }
    request->config = candidate;
    subsystem->staged = candidate;
    subsystem->health.prepared_requests++;
    subsystem->health.pending_requests++;
    subsystem->health.last_request_id = request->request_id;
    subsystem->health.last_status = SYS_OK;
    return SYS_OK;
}

status_t config_subsystem_commit(
    config_subsystem_t *subsystem,
    const gateway_storage_config_request_t *request)
{
    status_t status;

    if (subsystem == 0 || request == 0 || subsystem->initialized == 0u) {
        return ERR_INVALID_ARG;
    }
    status = config_subsystem_validate(&request->config);
    if (status != SYS_OK || subsystem->health.pending_requests == 0u ||
        request->config.revision <= subsystem->active.revision) {
        subsystem->health.last_status = status != SYS_OK
            ? status : ERR_INVALID_ARG;
        return subsystem->health.last_status;
    }
    subsystem->active = request->config;
    if (request->config.revision >= subsystem->staged.revision) {
        subsystem->staged = request->config;
    }
    subsystem->health.committed_requests++;
    if (subsystem->health.pending_requests != 0u) {
        subsystem->health.pending_requests--;
    }
    subsystem->health.last_request_id = request->request_id;
    subsystem->health.active_revision = request->config.revision;
    subsystem->health.last_status = SYS_OK;
    return SYS_OK;
}

status_t config_subsystem_reject(config_subsystem_t *subsystem,
                                 uint32_t request_id, status_t reason)
{
    if (subsystem == 0 || subsystem->initialized == 0u ||
        subsystem->health.pending_requests == 0u || reason == SYS_OK) {
        return ERR_INVALID_ARG;
    }
    subsystem->health.rejected_requests++;
    if (subsystem->health.pending_requests != 0u) {
        subsystem->health.pending_requests--;
    }
    subsystem->health.last_request_id = request_id;
    subsystem->health.last_status = reason;
    if (subsystem->health.pending_requests == 0u) {
        subsystem->staged = subsystem->active;
    }
    return SYS_OK;
}

status_t config_subsystem_get(const config_subsystem_t *subsystem,
                              gateway_runtime_config_t *config)
{
    if (subsystem == 0 || config == 0 || subsystem->initialized == 0u) {
        return ERR_INVALID_ARG;
    }
    *config = subsystem->active;
    return SYS_OK;
}

status_t config_subsystem_get_health(const config_subsystem_t *subsystem,
                                     config_health_t *health)
{
    if (subsystem == 0 || health == 0 || subsystem->initialized == 0u) {
        return ERR_INVALID_ARG;
    }
    *health = subsystem->health;
    return SYS_OK;
}
