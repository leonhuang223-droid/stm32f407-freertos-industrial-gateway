#ifndef GATEWAY_CONFIG_SUBSYSTEM_H
#define GATEWAY_CONFIG_SUBSYSTEM_H

#include "gateway_model.h"

#include <stdint.h>

typedef enum {
    CONFIG_FIELD_HIGH_THRESHOLD = 0,
    CONFIG_FIELD_LOW_THRESHOLD,
    CONFIG_FIELD_HYSTERESIS,
    CONFIG_FIELD_ASSERT_SAMPLES,
    CONFIG_FIELD_RECOVER_SAMPLES,
    CONFIG_FIELD_HIGH_ENABLED,
    CONFIG_FIELD_LOW_ENABLED,
    CONFIG_FIELD_RELAY_ON_ALARM,
    CONFIG_FIELD_RELAY_SAFE_ENERGIZED
} config_field_t;

typedef struct {
    uint16_t point_id;
    config_field_t field;
    int32_t value;
} config_patch_t;

typedef struct {
    uint32_t prepared_requests;
    uint32_t committed_requests;
    uint32_t rejected_requests;
    uint32_t pending_requests;
    uint32_t last_request_id;
    uint32_t active_revision;
    status_t last_status;
} config_health_t;

typedef struct {
    gateway_runtime_config_t active;
    gateway_runtime_config_t staged;
    config_health_t health;
    uint32_t next_request_id;
    uint8_t initialized;
} config_subsystem_t;

status_t config_subsystem_validate(const gateway_runtime_config_t *config);
status_t config_subsystem_construct(config_subsystem_t *subsystem,
                                    const gateway_runtime_config_t *config);
status_t config_subsystem_prepare(config_subsystem_t *subsystem,
                                  const config_patch_t *patch,
                                  gateway_storage_config_request_t *request);
status_t config_subsystem_commit(
    config_subsystem_t *subsystem,
    const gateway_storage_config_request_t *request);
status_t config_subsystem_reject(config_subsystem_t *subsystem,
                                 uint32_t request_id, status_t reason);
status_t config_subsystem_get(const config_subsystem_t *subsystem,
                              gateway_runtime_config_t *config);
status_t config_subsystem_get_health(const config_subsystem_t *subsystem,
                                     config_health_t *health);

#endif
