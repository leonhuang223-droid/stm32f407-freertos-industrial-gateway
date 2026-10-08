#ifndef GATEWAY_BOOT_CONFIRMATION_H
#define GATEWAY_BOOT_CONFIRMATION_H

#include "boot_metadata.h"

#include <stdint.h>

typedef struct {
    uint32_t attempts;
    uint32_t commits;
    uint32_t metadata_sequence;
    app_slot_t running_slot;
    app_slot_t active_slot;
    app_slot_t pending_slot;
    boot_state_t boot_state;
    status_t last_error;
    uint8_t confirmed;
} boot_confirmation_health_t;

typedef struct {
    boot_meta_store_t store;
    boot_confirmation_health_t health;
    uint8_t initialized;
    uint8_t attempted;
} boot_confirmation_t;

status_t boot_confirmation_construct(boot_confirmation_t *confirmation,
                                     const boot_meta_store_t *store,
                                     app_slot_t running_slot);
status_t boot_confirmation_confirm(boot_confirmation_t *confirmation);
status_t boot_confirmation_get_health(const boot_confirmation_t *confirmation,
                                      boot_confirmation_health_t *health);

#endif
