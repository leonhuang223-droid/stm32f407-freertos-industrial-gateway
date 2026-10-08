#include "relay.h"

#include <string.h>

static int state_valid(relay_state_t state)
{
    return state == RELAY_DEENERGIZED || state == RELAY_ENERGIZED;
}

static int relay_valid(const relay_t *relay)
{
    return relay != 0 && relay->ops != 0 && relay->context != 0 &&
           relay->ops->init != 0 && relay->ops->write != 0;
}

static int physical_level(const relay_t *relay, relay_state_t state)
{
    int active = state == RELAY_ENERGIZED ? 1 : 0;

    return relay->config.active_high != 0u ? active : !active;
}

status_t relay_construct(relay_t *relay,
                         const relay_ops_t *ops,
                         void *context,
                         const relay_config_t *config)
{
    if (relay == 0 || ops == 0 || context == 0 || config == 0 ||
        ops->init == 0 || ops->write == 0 || config->active_high > 1u ||
        !state_valid(config->safe_state)) {
        return ERR_INVALID_ARG;
    }
    memset(relay, 0, sizeof(*relay));
    relay->ops = ops;
    relay->context = context;
    relay->config = *config;
    relay->state = config->safe_state;
    relay->health.last_error = ERR_DEVICE_NOT_READY;
    return SYS_OK;
}

status_t relay_init(relay_t *relay)
{
    status_t status;

    if (!relay_valid(relay)) {
        return ERR_INVALID_ARG;
    }
    status = relay->ops->init(relay->context,
                              physical_level(relay, relay->config.safe_state));
    if (status == SYS_OK) {
        status = relay->ops->write(
            relay->context, physical_level(relay, relay->config.safe_state));
    }
    relay->health.last_error = status;
    relay->initialized = status == SYS_OK ? 1u : 0u;
    relay->suspended = 0u;
    if (status == SYS_OK) {
        relay->state = relay->config.safe_state;
    } else {
        relay->health.write_errors++;
    }
    return status;
}

status_t relay_set(relay_t *relay, relay_state_t state)
{
    status_t status;

    if (!relay_valid(relay) || !state_valid(state)) {
        return ERR_INVALID_ARG;
    }
    if (relay->initialized == 0u || relay->suspended != 0u) {
        return ERR_DEVICE_NOT_READY;
    }
    if (relay->state == state) {
        return SYS_OK;
    }
    status = relay->ops->write(relay->context, physical_level(relay, state));
    relay->health.last_error = status;
    if (status == SYS_OK) {
        relay->state = state;
        relay->health.transitions++;
    } else {
        relay->health.write_errors++;
    }
    return status;
}

status_t relay_force_safe(relay_t *relay)
{
    return relay_set(relay,
                     relay != 0 ? relay->config.safe_state : RELAY_DEENERGIZED);
}

status_t relay_configure_safe_state(relay_t *relay, relay_state_t state)
{
    if (!relay_valid(relay) || !state_valid(state)) {
        return ERR_INVALID_ARG;
    }
    relay->config.safe_state = state;
    return SYS_OK;
}

status_t relay_get_health(const relay_t *relay, relay_health_t *health)
{
    if (!relay_valid(relay) || health == 0) {
        return ERR_INVALID_ARG;
    }
    *health = relay->health;
    return SYS_OK;
}

status_t relay_get_state(const relay_t *relay, relay_state_t *state)
{
    if (relay == 0 || state == 0 || relay->initialized == 0u) {
        return ERR_INVALID_ARG;
    }
    *state = relay->state;
    return SYS_OK;
}

status_t relay_suspend(relay_t *relay)
{
    status_t status;

    if (!relay_valid(relay) || relay->initialized == 0u) {
        return ERR_DEVICE_NOT_READY;
    }
    status = relay_force_safe(relay);
    if (status == SYS_OK && relay->ops->suspend != 0) {
        status = relay->ops->suspend(relay->context);
    }
    if (status == SYS_OK) {
        relay->suspended = 1u;
    }
    relay->health.last_error = status;
    return status;
}

status_t relay_resume(relay_t *relay)
{
    status_t status = SYS_OK;

    if (!relay_valid(relay) || relay->initialized == 0u) {
        return ERR_DEVICE_NOT_READY;
    }
    if (relay->ops->resume != 0) {
        status = relay->ops->resume(relay->context);
    }
    if (status == SYS_OK) {
        status = relay->ops->write(
            relay->context, physical_level(relay, relay->config.safe_state));
    }
    if (status == SYS_OK) {
        relay->suspended = 0u;
        relay->state = relay->config.safe_state;
    } else {
        /* Keep commands inhibited until a safe output is confirmed. */
        relay->suspended = 1u;
        relay->health.write_errors++;
    }
    relay->health.last_error = status;
    return status;
}
