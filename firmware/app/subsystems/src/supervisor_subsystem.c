#include "supervisor_subsystem.h"

#include <string.h>

static int config_valid(const supervisor_config_t *config)
{
    unsigned int i;

    if (config == 0 || config->task_count == 0u ||
        config->task_count > SUPERVISOR_MAX_TASKS ||
        config->critical_task_mask == 0u || config->startup_grace_ms == 0u ||
        config->boot_confirm_stable_ms == 0u ||
        (config->critical_task_mask >> config->task_count) != 0u) {
        return 0;
    }
    for (i = 0u; i < config->task_count; ++i) {
        if ((config->critical_task_mask & (1UL << i)) != 0u &&
            config->task_timeout_ms[i] == 0u) {
            return 0;
        }
    }
    return 1;
}

status_t supervisor_subsystem_construct(supervisor_subsystem_t *supervisor,
                                        watchdog_device_t *watchdog,
                                        const supervisor_config_t *config)
{
    if (supervisor == 0 || watchdog == 0 || watchdog->started == 0u ||
        !config_valid(config)) {
        return ERR_INVALID_ARG;
    }
    memset(supervisor, 0, sizeof(*supervisor));
    supervisor->config = *config;
    supervisor->watchdog = watchdog;
    supervisor->health.last_error = SYS_OK;
    supervisor->initialized = 1u;
    return SYS_OK;
}

status_t supervisor_subsystem_start(supervisor_subsystem_t *supervisor,
                                    uint32_t now_ms,
                                    const uint32_t *heartbeat)
{
    unsigned int i;

    if (supervisor == 0 || supervisor->initialized == 0u || heartbeat == 0) {
        return ERR_INVALID_ARG;
    }
    supervisor->started_ms = now_ms;
    for (i = 0u; i < supervisor->config.task_count; ++i) {
        supervisor->previous_heartbeat[i] = heartbeat[i];
        supervisor->last_progress_ms[i] = now_ms;
    }
    supervisor->started = 1u;
    return SYS_OK;
}

status_t supervisor_subsystem_process(supervisor_subsystem_t *supervisor,
                                      uint32_t now_ms,
                                      const uint32_t *heartbeat,
                                      uint8_t services_ready)
{
    uint32_t stale_mask = 0u;
    uint32_t critical_seen;
    unsigned int i;
    status_t status = SYS_OK;

    if (supervisor == 0 || supervisor->started == 0u || heartbeat == 0) {
        return ERR_DEVICE_NOT_READY;
    }
    supervisor->health.checks++;
    for (i = 0u; i < supervisor->config.task_count; ++i) {
        uint32_t bit = 1UL << i;

        if ((supervisor->config.critical_task_mask & bit) == 0u) {
            continue;
        }
        if (heartbeat[i] != supervisor->previous_heartbeat[i]) {
            supervisor->previous_heartbeat[i] = heartbeat[i];
            supervisor->last_progress_ms[i] = now_ms;
            supervisor->health.seen_task_mask |= bit;
        } else if ((supervisor->health.seen_task_mask & bit) != 0u) {
            if (now_ms - supervisor->last_progress_ms[i] >
                supervisor->config.task_timeout_ms[i]) {
                stale_mask |= bit;
            }
        } else if (now_ms - supervisor->started_ms >
                   supervisor->config.startup_grace_ms) {
            stale_mask |= bit;
        }
    }
    supervisor->health.stale_task_mask = stale_mask;
    critical_seen = supervisor->health.seen_task_mask &
                    supervisor->config.critical_task_mask;
    supervisor->health.healthy =
        services_ready != 0u && stale_mask == 0u &&
        critical_seen == supervisor->config.critical_task_mask &&
        supervisor->health.latched_faults == 0u;

    if (supervisor->health.healthy != 0u) {
        if (supervisor->stable_window_active == 0u) {
            supervisor->healthy_since_ms = now_ms;
            supervisor->stable_window_active = 1u;
        }
        status = watchdog_device_refresh(supervisor->watchdog);
        if (status == SYS_OK) {
            supervisor->health.watchdog_refreshes++;
            supervisor->health.healthy_checks++;
            supervisor->health.last_error = SYS_OK;
        } else {
            supervisor->health.watchdog_failures++;
            supervisor_subsystem_latch_fault(supervisor, status);
        }
    } else {
        supervisor->stable_window_active = 0u;
        supervisor->health.unhealthy_checks++;
        if (stale_mask != 0u) {
            supervisor->health.last_error = ERR_TIMEOUT;
            status = ERR_TIMEOUT;
        } else if (supervisor->health.latched_faults != 0u) {
            status = supervisor->health.last_error;
        } else {
            supervisor->health.last_error = ERR_DEVICE_NOT_READY;
            status = ERR_DEVICE_NOT_READY;
        }
    }
    return status;
}

void supervisor_subsystem_latch_fault(supervisor_subsystem_t *supervisor,
                                      status_t error)
{
    if (supervisor != 0 && supervisor->initialized != 0u && error != SYS_OK) {
        supervisor->health.latched_faults++;
        supervisor->health.last_error = error;
        supervisor->health.healthy = 0u;
        supervisor->stable_window_active = 0u;
    }
}

uint8_t supervisor_subsystem_boot_confirm_ready(
    const supervisor_subsystem_t *supervisor, uint32_t now_ms)
{
    return supervisor != 0 && supervisor->health.healthy != 0u &&
                   supervisor->stable_window_active != 0u &&
                   now_ms - supervisor->healthy_since_ms >=
                       supervisor->config.boot_confirm_stable_ms
               ? 1u
               : 0u;
}

status_t
supervisor_subsystem_get_health(const supervisor_subsystem_t *supervisor,
                                supervisor_health_t *health)
{
    if (supervisor == 0 || supervisor->initialized == 0u || health == 0) {
        return ERR_INVALID_ARG;
    }
    *health = supervisor->health;
    return SYS_OK;
}
