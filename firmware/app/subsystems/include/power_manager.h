#ifndef GATEWAY_POWER_MANAGER_H
#define GATEWAY_POWER_MANAGER_H

#include "error_code.h"

#include <stdint.h>

typedef enum {
    POWER_ACTIVE = 0,
    POWER_ECO,
    POWER_TICKLESS_SLEEP,
    POWER_STOP_PERIODIC,
    POWER_STANDBY_SHIPPING,
    POWER_MODE_COUNT
} power_mode_t;

typedef enum {
    POWER_POLICY_AUTO = 0,
    POWER_POLICY_FORCE_ACTIVE,
    POWER_POLICY_FORCE_ECO
} power_policy_t;

typedef enum {
    PM_LOCK_OTA = 0,
    PM_LOCK_FLASH_WRITE,
    PM_LOCK_NETWORK_TX,
    PM_LOCK_MODBUS_TRANSACTION,
    PM_LOCK_CAN_MONITORING,
    PM_LOCK_UI_ACTIVE,
    PM_LOCK_ALARM_ACTIVE,
    PM_LOCK_COUNT
} power_lock_id_t;

typedef enum {
    POWER_SLEEP_REJECT_TOO_SHORT = 0,
    POWER_SLEEP_REJECT_LOCKED,
    POWER_SLEEP_REJECT_WATCHDOG_WINDOW,
    POWER_SLEEP_REJECT_COUNT
} power_sleep_reject_t;

typedef enum {
    POWER_WAKE_SYSTICK = 0,
    POWER_WAKE_INTERRUPT,
    POWER_WAKE_UNKNOWN,
    POWER_WAKE_COUNT
} power_wake_reason_t;

typedef struct {
    uint32_t auto_eco_after_ms;
    uint32_t minimum_tickless_ms;
    uint32_t watchdog_margin_ms;
    uint32_t lock_leak_timeout_ms;
    uint32_t persistent_lock_mask;
} power_manager_config_t;

typedef struct {
    uint32_t lock_mask;
    uint32_t leak_mask;
    uint32_t acquire_count;
    uint32_t release_count;
    uint32_t release_errors;
    uint32_t overflow_errors;
    uint32_t leak_events;
    uint32_t mode_transitions;
    uint32_t sleep_attempts;
    uint32_t sleep_entries;
    uint32_t sleep_rejections[POWER_SLEEP_REJECT_COUNT];
    uint32_t wake_count[POWER_WAKE_COUNT];
    uint32_t cumulative_sleep_budget_ms;
    uint32_t longest_sleep_budget_ms;
    uint32_t longest_lock_ms[PM_LOCK_COUNT];
    uint32_t mode_residency_ms[POWER_MODE_COUNT];
} power_manager_stats_t;

typedef struct {
    power_manager_config_t config;
    power_manager_stats_t stats;
    uint16_t lock_refcount[PM_LOCK_COUNT];
    uint32_t lock_acquired_ms[PM_LOCK_COUNT];
    uint32_t leak_reported_mask;
    uint32_t last_activity_ms;
    uint32_t last_evaluation_ms;
    power_mode_t current_mode;
    power_policy_t policy;
    status_t last_error;
    uint8_t initialized;
} power_manager_t;

status_t power_manager_construct(power_manager_t *manager,
                                 const power_manager_config_t *config,
                                 uint32_t now_ms);
status_t power_manager_acquire(power_manager_t *manager,
                               power_lock_id_t lock, uint32_t now_ms);
status_t power_manager_release(power_manager_t *manager,
                               power_lock_id_t lock, uint32_t now_ms);
power_mode_t power_manager_deepest_allowed(const power_manager_t *manager);
status_t power_manager_set_policy(power_manager_t *manager,
                                  power_policy_t policy, uint32_t now_ms);
void power_manager_note_activity(power_manager_t *manager,
                                 uint32_t now_ms);
power_mode_t power_manager_evaluate(power_manager_t *manager,
                                    uint32_t now_ms,
                                    uint8_t alarm_active);
status_t power_manager_prepare_tickless(power_manager_t *manager,
                                        uint32_t requested_ms,
                                        uint32_t watchdog_remaining_ms,
                                        uint32_t *allowed_ms);
void power_manager_record_wake(power_manager_t *manager,
                               uint32_t planned_sleep_ms,
                               power_wake_reason_t reason);
status_t power_manager_get_stats(const power_manager_t *manager,
                                 power_manager_stats_t *stats);

#endif
