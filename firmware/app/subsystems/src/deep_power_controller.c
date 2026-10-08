#include "deep_power_controller.h"

#include <string.h>

static int controller_ready(const deep_power_controller_t *controller)
{
    return controller != 0 && controller->initialized != 0u;
}

static status_t fail_request(deep_power_controller_t *controller,
                             status_t status)
{
    controller->health.state = DEEP_POWER_FAILED;
    controller->health.last_error = status;
    controller->health.failures++;
    controller->health.request_pending = 0u;
    return status;
}

status_t
deep_power_controller_construct(deep_power_controller_t *controller,
                                const deep_power_platform_ops_t *ops,
                                void *platform_context,
                                const deep_power_controller_config_t *config)
{
    if (controller == 0 || ops == 0 || config == 0 || ops->enter_stop == 0 ||
        ops->enter_standby == 0 || config->minimum_stop_ms == 0u ||
        config->maximum_stop_ms < config->minimum_stop_ms ||
        config->watchdog_margin_ms == 0u ||
        config->watchdog_margin_ms > UINT32_MAX - config->maximum_stop_ms ||
        config->required_quiesce_mask == 0u ||
        (config->required_quiesce_mask &
         ~(uint32_t)DEEP_POWER_PARTICIPANT_ALL) != 0u ||
        config->quiesce_timeout_ms == 0u ||
        config->quiesce_timeout_ms > INT32_MAX || config->stop_enabled > 1u ||
        config->standby_enabled > 1u) {
        return ERR_INVALID_ARG;
    }
    memset(controller, 0, sizeof(*controller));
    controller->ops = ops;
    controller->platform_context = platform_context;
    controller->config = *config;
    controller->health.state = DEEP_POWER_IDLE;
    controller->health.requested_mode = POWER_ACTIVE;
    controller->health.last_wake_reason = POWER_WAKE_UNKNOWN;
    controller->health.last_error = SYS_OK;
    controller->initialized = 1u;
    return SYS_OK;
}

status_t deep_power_controller_request(deep_power_controller_t *controller,
                                       power_mode_t mode,
                                       uint32_t requested_ms,
                                       uint32_t confirmation,
                                       uint32_t now_ms)
{
    if (!controller_ready(controller) ||
        confirmation != DEEP_POWER_CONFIRMATION) {
        return ERR_INVALID_ARG;
    }
    if (controller->health.request_pending != 0u) {
        return ERR_DEVICE_NOT_READY;
    }
    if (mode == POWER_STOP_PERIODIC) {
        if (controller->config.stop_enabled == 0u) {
            return ERR_UNSUPPORTED;
        }
        if (requested_ms < controller->config.minimum_stop_ms ||
            requested_ms > controller->config.maximum_stop_ms) {
            return ERR_INVALID_ARG;
        }
    } else if (mode == POWER_STANDBY_SHIPPING) {
        if (controller->config.standby_enabled == 0u) {
            return ERR_UNSUPPORTED;
        }
        if (requested_ms != 0u) {
            return ERR_INVALID_ARG;
        }
    } else {
        return ERR_INVALID_ARG;
    }

    controller->health.state = DEEP_POWER_WAITING_QUIESCE;
    controller->health.requested_mode = mode;
    controller->health.requested_ms = requested_ms;
    controller->health.request_count++;
    controller->health.last_error = SYS_OK;
    controller->health.request_pending = 1u;
    controller->request_started_ms = now_ms;
    return SYS_OK;
}

status_t deep_power_controller_cancel(deep_power_controller_t *controller)
{
    if (!controller_ready(controller)) {
        return ERR_INVALID_ARG;
    }
    if (controller->health.state == DEEP_POWER_ENTERING ||
        controller->health.state == DEEP_POWER_RESTORING) {
        return ERR_DEVICE_NOT_READY;
    }
    if (controller->health.request_pending != 0u) {
        controller->health.cancel_count++;
    }
    controller->health.request_pending = 0u;
    controller->health.state = DEEP_POWER_IDLE;
    controller->health.requested_mode = POWER_ACTIVE;
    controller->health.requested_ms = 0u;
    controller->health.last_error = SYS_OK;
    return SYS_OK;
}

status_t deep_power_controller_process(deep_power_controller_t *controller,
                                       uint32_t quiesced_mask,
                                       power_mode_t deepest_allowed,
                                       uint32_t watchdog_remaining_ms,
                                       uint32_t now_ms)
{
    status_t status;

    if (!controller_ready(controller)) {
        return ERR_INVALID_ARG;
    }
    if (controller->health.request_pending == 0u) {
        return ERR_DEVICE_NOT_READY;
    }
    if (now_ms - controller->request_started_ms >=
        controller->config.quiesce_timeout_ms) {
        return fail_request(controller, ERR_TIMEOUT);
    }
    if ((uint32_t)deepest_allowed > POWER_STANDBY_SHIPPING) {
        return fail_request(controller, ERR_INVALID_ARG);
    }
    if ((quiesced_mask & controller->config.required_quiesce_mask) !=
        controller->config.required_quiesce_mask) {
        controller->health.quiesce_waits++;
        controller->health.last_error = ERR_DEVICE_NOT_READY;
        return ERR_DEVICE_NOT_READY;
    }
    if (deepest_allowed < controller->health.requested_mode) {
        controller->health.lock_rejections++;
        controller->health.last_error = ERR_DEVICE_NOT_READY;
        return ERR_DEVICE_NOT_READY;
    }
    if (controller->health.requested_mode == POWER_STOP_PERIODIC &&
        watchdog_remaining_ms <= controller->health.requested_ms +
                                     controller->config.watchdog_margin_ms) {
        controller->health.watchdog_rejections++;
        controller->health.last_error = ERR_TIMEOUT;
        return ERR_TIMEOUT;
    }

    controller->health.state = DEEP_POWER_ENTERING;
    if (controller->health.requested_mode == POWER_STOP_PERIODIC) {
        uint32_t elapsed_ms = 0u;
        power_wake_reason_t reason = POWER_WAKE_UNKNOWN;

        status = controller->ops->enter_stop(controller->platform_context,
                                             controller->health.requested_ms,
                                             &elapsed_ms,
                                             &reason);
        if (status != SYS_OK) {
            return fail_request(controller, status);
        }
        controller->health.state = DEEP_POWER_RESTORING;
        controller->health.stop_entries++;
        controller->health.restore_count++;
        controller->health.last_elapsed_ms = elapsed_ms;
        controller->health.last_wake_reason =
            (unsigned int)reason < POWER_WAKE_COUNT ? reason
                                                    : POWER_WAKE_UNKNOWN;
        controller->health.request_pending = 0u;
        controller->health.state = DEEP_POWER_IDLE;
        controller->health.requested_mode = POWER_ACTIVE;
        controller->health.requested_ms = 0u;
        controller->health.last_error = SYS_OK;
        return SYS_OK;
    }

    controller->health.standby_entries++;
    status = controller->ops->enter_standby(controller->platform_context);
    return fail_request(controller, status == SYS_OK ? ERR_IO : status);
}

status_t
deep_power_controller_get_health(const deep_power_controller_t *controller,
                                 deep_power_health_t *health)
{
    if (!controller_ready(controller) || health == 0) {
        return ERR_INVALID_ARG;
    }
    *health = controller->health;
    return SYS_OK;
}
