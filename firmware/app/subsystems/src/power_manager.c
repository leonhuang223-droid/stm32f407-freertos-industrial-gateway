#include "power_manager.h"

#include <limits.h>
#include <string.h>

static const power_mode_t lock_limits[PM_LOCK_COUNT] = {
    POWER_TICKLESS_SLEEP,
    POWER_ACTIVE,
    POWER_TICKLESS_SLEEP,
    POWER_TICKLESS_SLEEP,
    POWER_TICKLESS_SLEEP,
    POWER_ACTIVE,
    POWER_ACTIVE
};

static int manager_ready(const power_manager_t *manager)
{
    return manager != 0 && manager->initialized != 0u;
}

static void transition_mode(power_manager_t *manager, power_mode_t mode,
                            uint32_t now_ms)
{
    uint32_t elapsed = now_ms - manager->last_evaluation_ms;

    manager->stats.mode_residency_ms[manager->current_mode] += elapsed;
    manager->last_evaluation_ms = now_ms;
    if (manager->current_mode != mode) {
        manager->current_mode = mode;
        manager->stats.mode_transitions++;
    }
}

status_t power_manager_construct(power_manager_t *manager,
                                 const power_manager_config_t *config,
                                 uint32_t now_ms)
{
    if (manager == 0 || config == 0 ||
        config->minimum_tickless_ms == 0u ||
        config->watchdog_margin_ms == 0u ||
        config->lock_leak_timeout_ms == 0u ||
        (config->persistent_lock_mask >> PM_LOCK_COUNT) != 0u) {
        return ERR_INVALID_ARG;
    }
    memset(manager, 0, sizeof(*manager));
    manager->config = *config;
    manager->current_mode = POWER_ACTIVE;
    manager->policy = POWER_POLICY_AUTO;
    manager->last_activity_ms = now_ms;
    manager->last_evaluation_ms = now_ms;
    manager->last_error = SYS_OK;
    manager->initialized = 1u;
    return SYS_OK;
}

status_t power_manager_acquire(power_manager_t *manager,
                               power_lock_id_t lock, uint32_t now_ms)
{
    uint16_t *count;

    if (!manager_ready(manager) || (unsigned int)lock >= PM_LOCK_COUNT) {
        return ERR_INVALID_ARG;
    }
    count = &manager->lock_refcount[(unsigned int)lock];
    if (*count == UINT16_MAX) {
        manager->stats.overflow_errors++;
        manager->last_error = ERR_NO_MEMORY;
        return ERR_NO_MEMORY;
    }
    if (*count == 0u) {
        manager->lock_acquired_ms[(unsigned int)lock] = now_ms;
        manager->stats.lock_mask |= 1UL << (unsigned int)lock;
        manager->stats.leak_mask &= ~(1UL << (unsigned int)lock);
        manager->leak_reported_mask &= ~(1UL << (unsigned int)lock);
    }
    (*count)++;
    manager->stats.acquire_count++;
    manager->last_error = SYS_OK;
    return SYS_OK;
}

status_t power_manager_release(power_manager_t *manager,
                               power_lock_id_t lock, uint32_t now_ms)
{
    uint16_t *count;
    uint32_t held_ms;

    if (!manager_ready(manager) || (unsigned int)lock >= PM_LOCK_COUNT) {
        return ERR_INVALID_ARG;
    }
    count = &manager->lock_refcount[(unsigned int)lock];
    if (*count == 0u) {
        manager->stats.release_errors++;
        manager->last_error = ERR_INVALID_ARG;
        return ERR_INVALID_ARG;
    }
    (*count)--;
    manager->stats.release_count++;
    if (*count == 0u) {
        held_ms = now_ms - manager->lock_acquired_ms[(unsigned int)lock];
        if (held_ms > manager->stats.longest_lock_ms[(unsigned int)lock]) {
            manager->stats.longest_lock_ms[(unsigned int)lock] = held_ms;
        }
        manager->stats.lock_mask &= ~(1UL << (unsigned int)lock);
        manager->stats.leak_mask &= ~(1UL << (unsigned int)lock);
        manager->leak_reported_mask &= ~(1UL << (unsigned int)lock);
    }
    manager->last_error = SYS_OK;
    return SYS_OK;
}

power_mode_t power_manager_deepest_allowed(const power_manager_t *manager)
{
    power_mode_t deepest = POWER_STANDBY_SHIPPING;
    unsigned int i;

    if (!manager_ready(manager)) {
        return POWER_ACTIVE;
    }
    for (i = 0u; i < PM_LOCK_COUNT; ++i) {
        if (manager->lock_refcount[i] != 0u && lock_limits[i] < deepest) {
            deepest = lock_limits[i];
        }
    }
    return deepest;
}

status_t power_manager_set_policy(power_manager_t *manager,
                                  power_policy_t policy, uint32_t now_ms)
{
    if (!manager_ready(manager) ||
        (unsigned int)policy > POWER_POLICY_FORCE_ECO) {
        return ERR_INVALID_ARG;
    }
    transition_mode(manager, manager->current_mode, now_ms);
    manager->policy = policy;
    manager->last_error = SYS_OK;
    return SYS_OK;
}

void power_manager_note_activity(power_manager_t *manager, uint32_t now_ms)
{
    if (manager_ready(manager)) {
        manager->last_activity_ms = now_ms;
    }
}

power_mode_t power_manager_evaluate(power_manager_t *manager,
                                    uint32_t now_ms,
                                    uint8_t alarm_active)
{
    power_mode_t desired = POWER_ACTIVE;
    unsigned int i;

    if (!manager_ready(manager)) {
        return POWER_ACTIVE;
    }
    for (i = 0u; i < PM_LOCK_COUNT; ++i) {
        uint32_t bit = 1UL << i;

        if (manager->lock_refcount[i] != 0u &&
            (manager->config.persistent_lock_mask & bit) == 0u &&
            now_ms - manager->lock_acquired_ms[i] >=
                manager->config.lock_leak_timeout_ms) {
            manager->stats.leak_mask |= bit;
            if ((manager->leak_reported_mask & bit) == 0u) {
                manager->leak_reported_mask |= bit;
                manager->stats.leak_events++;
            }
        }
    }

    if (alarm_active == 0u &&
        power_manager_deepest_allowed(manager) != POWER_ACTIVE) {
        if (manager->policy == POWER_POLICY_FORCE_ECO ||
            (manager->policy == POWER_POLICY_AUTO &&
             now_ms - manager->last_activity_ms >=
                 manager->config.auto_eco_after_ms)) {
            desired = POWER_ECO;
        }
    }
    transition_mode(manager, desired, now_ms);
    return desired;
}

status_t power_manager_prepare_tickless(power_manager_t *manager,
                                        uint32_t requested_ms,
                                        uint32_t watchdog_remaining_ms,
                                        uint32_t *allowed_ms)
{
    uint32_t limit;

    if (!manager_ready(manager) || allowed_ms == 0) {
        return ERR_INVALID_ARG;
    }
    *allowed_ms = 0u;
    manager->stats.sleep_attempts++;
    if (requested_ms < manager->config.minimum_tickless_ms) {
        manager->stats.sleep_rejections[POWER_SLEEP_REJECT_TOO_SHORT]++;
        manager->last_error = ERR_TIMEOUT;
        return ERR_TIMEOUT;
    }
    if (power_manager_deepest_allowed(manager) < POWER_TICKLESS_SLEEP) {
        manager->stats.sleep_rejections[POWER_SLEEP_REJECT_LOCKED]++;
        manager->last_error = ERR_DEVICE_NOT_READY;
        return ERR_DEVICE_NOT_READY;
    }
    if (watchdog_remaining_ms <= manager->config.watchdog_margin_ms +
                                     manager->config.minimum_tickless_ms) {
        manager->stats.sleep_rejections[POWER_SLEEP_REJECT_WATCHDOG_WINDOW]++;
        manager->last_error = ERR_TIMEOUT;
        return ERR_TIMEOUT;
    }
    limit = watchdog_remaining_ms - manager->config.watchdog_margin_ms;
    if (requested_ms > limit) {
        manager->stats.sleep_rejections[POWER_SLEEP_REJECT_WATCHDOG_WINDOW]++;
        manager->last_error = ERR_TIMEOUT;
        return ERR_TIMEOUT;
    }
    *allowed_ms = requested_ms;
    manager->last_error = SYS_OK;
    return SYS_OK;
}

void power_manager_record_wake(power_manager_t *manager,
                               uint32_t planned_sleep_ms,
                               power_wake_reason_t reason)
{
    if (!manager_ready(manager) || planned_sleep_ms == 0u) {
        return;
    }
    if ((unsigned int)reason >= POWER_WAKE_COUNT) {
        reason = POWER_WAKE_UNKNOWN;
    }
    manager->stats.sleep_entries++;
    manager->stats.cumulative_sleep_budget_ms += planned_sleep_ms;
    manager->stats.mode_residency_ms[POWER_TICKLESS_SLEEP] +=
        planned_sleep_ms;
    manager->stats.wake_count[(unsigned int)reason]++;
    if (planned_sleep_ms > manager->stats.longest_sleep_budget_ms) {
        manager->stats.longest_sleep_budget_ms = planned_sleep_ms;
    }
}

status_t power_manager_get_stats(const power_manager_t *manager,
                                 power_manager_stats_t *stats)
{
    if (!manager_ready(manager) || stats == 0) {
        return ERR_INVALID_ARG;
    }
    *stats = manager->stats;
    return SYS_OK;
}
