#ifndef GATEWAY_DEEP_POWER_CONTROLLER_H
#define GATEWAY_DEEP_POWER_CONTROLLER_H

#include "power_manager.h"

#include <stdint.h>

#define DEEP_POWER_CONFIRMATION 0x504f5752u

enum {
    DEEP_POWER_PARTICIPANT_ACQUISITION = 1UL << 0,
    DEEP_POWER_PARTICIPANT_MODBUS = 1UL << 1,
    DEEP_POWER_PARTICIPANT_CAN = 1UL << 2,
    DEEP_POWER_PARTICIPANT_NETWORK = 1UL << 3,
    DEEP_POWER_PARTICIPANT_STORAGE = 1UL << 4,
    DEEP_POWER_PARTICIPANT_UI = 1UL << 5,
    DEEP_POWER_PARTICIPANT_ALL = DEEP_POWER_PARTICIPANT_ACQUISITION |
        DEEP_POWER_PARTICIPANT_MODBUS | DEEP_POWER_PARTICIPANT_CAN |
        DEEP_POWER_PARTICIPANT_NETWORK | DEEP_POWER_PARTICIPANT_STORAGE |
        DEEP_POWER_PARTICIPANT_UI
};

typedef enum {
    DEEP_POWER_IDLE = 0,
    DEEP_POWER_WAITING_QUIESCE,
    DEEP_POWER_ENTERING,
    DEEP_POWER_RESTORING,
    DEEP_POWER_FAILED
} deep_power_state_t;

typedef struct {
    status_t (*enter_stop)(void *context,
                           uint32_t requested_ms,
                           uint32_t *elapsed_ms,
                           power_wake_reason_t *wake_reason);
    status_t (*enter_standby)(void *context);
} deep_power_platform_ops_t;

typedef struct {
    uint8_t stop_enabled;
    uint8_t standby_enabled;
    uint32_t minimum_stop_ms;
    uint32_t maximum_stop_ms;
    uint32_t watchdog_margin_ms;
    uint32_t required_quiesce_mask;
    uint32_t quiesce_timeout_ms;
} deep_power_controller_config_t;

typedef struct {
    deep_power_state_t state;
    power_mode_t requested_mode;
    uint32_t requested_ms;
    uint32_t request_count;
    uint32_t cancel_count;
    uint32_t quiesce_waits;
    uint32_t lock_rejections;
    uint32_t watchdog_rejections;
    uint32_t stop_entries;
    uint32_t standby_entries;
    uint32_t restore_count;
    uint32_t failures;
    uint32_t last_elapsed_ms;
    power_wake_reason_t last_wake_reason;
    status_t last_error;
    uint8_t request_pending;
} deep_power_health_t;

typedef struct {
    const deep_power_platform_ops_t *ops;
    void *platform_context;
    deep_power_controller_config_t config;
    deep_power_health_t health;
    uint32_t request_started_ms;
    uint8_t initialized;
} deep_power_controller_t;

status_t
deep_power_controller_construct(deep_power_controller_t *controller,
                                const deep_power_platform_ops_t *ops,
                                void *platform_context,
                                const deep_power_controller_config_t *config);
status_t deep_power_controller_request(deep_power_controller_t *controller,
                                       power_mode_t mode,
                                       uint32_t requested_ms,
                                       uint32_t confirmation,
                                       uint32_t now_ms);
status_t deep_power_controller_cancel(deep_power_controller_t *controller);
status_t deep_power_controller_process(deep_power_controller_t *controller,
                                       uint32_t quiesced_mask,
                                       power_mode_t deepest_allowed,
                                       uint32_t watchdog_remaining_ms,
                                       uint32_t now_ms);
status_t
deep_power_controller_get_health(const deep_power_controller_t *controller,
                                 deep_power_health_t *health);

#endif
