#ifndef GATEWAY_RELAY_H
#define GATEWAY_RELAY_H

#include "error_code.h"

#include <stdint.h>

typedef struct relay relay_t;

typedef enum { RELAY_DEENERGIZED = 0, RELAY_ENERGIZED = 1 } relay_state_t;

typedef struct {
    status_t (*init)(void *context, int inactive_level);
    status_t (*write)(void *context, int physical_level);
    status_t (*read)(void *context, int *physical_level);
    status_t (*suspend)(void *context);
    status_t (*resume)(void *context);
} relay_ops_t;

typedef struct {
    uint32_t transitions;
    uint32_t write_errors;
    status_t last_error;
} relay_health_t;

typedef struct {
    uint8_t active_high;
    relay_state_t safe_state;
} relay_config_t;

struct relay {
    const relay_ops_t *ops;
    void *context;
    relay_config_t config;
    relay_health_t health;
    relay_state_t state;
    uint8_t initialized;
    uint8_t suspended;
};

status_t relay_construct(relay_t *relay,
                         const relay_ops_t *ops,
                         void *context,
                         const relay_config_t *config);
status_t relay_init(relay_t *relay);
status_t relay_set(relay_t *relay, relay_state_t state);
status_t relay_force_safe(relay_t *relay);
/* Owner task only, or hold the same configuration lock as relay_set(). */
status_t relay_configure_safe_state(relay_t *relay, relay_state_t state);
status_t relay_get_health(const relay_t *relay, relay_health_t *health);
status_t relay_get_state(const relay_t *relay, relay_state_t *state);
status_t relay_suspend(relay_t *relay);
status_t relay_resume(relay_t *relay);

#endif
