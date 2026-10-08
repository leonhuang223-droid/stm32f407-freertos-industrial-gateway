#include "boot_confirmation.h"

#include <string.h>

static int slot_valid(app_slot_t slot)
{
    return slot == SLOT_A || slot == SLOT_B;
}

static void update_health(boot_confirmation_t *confirmation,
                          const boot_metadata_t *metadata)
{
    confirmation->health.metadata_sequence = metadata->sequence;
    confirmation->health.active_slot = metadata->active_slot;
    confirmation->health.pending_slot = metadata->pending_slot;
    confirmation->health.boot_state = metadata->boot_state;
}

status_t boot_confirmation_construct(boot_confirmation_t *confirmation,
                                     const boot_meta_store_t *store,
                                     app_slot_t running_slot)
{
    if (confirmation == 0 || store == 0 || store->read == 0 ||
        store->write == 0 || !slot_valid(running_slot)) {
        return ERR_INVALID_ARG;
    }
    memset(confirmation, 0, sizeof(*confirmation));
    confirmation->store = *store;
    confirmation->health.running_slot = running_slot;
    confirmation->health.active_slot = SLOT_NONE;
    confirmation->health.pending_slot = SLOT_NONE;
    confirmation->health.boot_state = BOOT_STATE_MAINTENANCE;
    confirmation->health.last_error = SYS_OK;
    confirmation->initialized = 1u;
    return SYS_OK;
}

status_t boot_confirmation_confirm(boot_confirmation_t *confirmation)
{
    boot_metadata_t current;
    boot_metadata_t desired;
    boot_metadata_t committed;
    app_slot_t current_copy;
    app_slot_t committed_copy;
    status_t status;

    if (confirmation == 0 || confirmation->initialized == 0u) {
        return ERR_INVALID_ARG;
    }
    if (confirmation->health.confirmed != 0u) {
        return SYS_OK;
    }
    if (confirmation->attempted != 0u) {
        return confirmation->health.last_error;
    }
    confirmation->attempted = 1u;
    confirmation->health.attempts++;

    status = boot_meta_load(&confirmation->store, &current, &current_copy);
    if (status != SYS_OK) {
        confirmation->health.last_error = status;
        return status;
    }
    update_health(confirmation, &current);
    if (current.boot_state == BOOT_STATE_NORMAL && current.boot_ok == 1u &&
        current.active_slot == confirmation->health.running_slot) {
        confirmation->health.confirmed = 1u;
        confirmation->health.last_error = SYS_OK;
        return SYS_OK;
    }
    if (current.boot_state != BOOT_STATE_TRIAL ||
        current.pending_slot != confirmation->health.running_slot) {
        confirmation->health.last_error = ERR_SLOT_MISMATCH;
        return ERR_SLOT_MISMATCH;
    }

    status = boot_meta_confirm_boot_ok(&current, &desired);
    if (status == SYS_OK) {
        status = boot_meta_commit(
            &confirmation->store,
            &(const boot_meta_commit_request_t){
                &current, current_copy, &desired, &committed, &committed_copy});
    }
    (void)committed_copy;
    if (status == SYS_OK) {
        update_health(confirmation, &committed);
        if (committed.boot_state != BOOT_STATE_NORMAL ||
            committed.boot_ok != 1u ||
            committed.active_slot != confirmation->health.running_slot) {
            status = ERR_FLASH_VERIFY;
        }
    }
    confirmation->health.last_error = status;
    if (status == SYS_OK) {
        confirmation->health.commits++;
        confirmation->health.confirmed = 1u;
    }
    return status;
}

status_t boot_confirmation_get_health(const boot_confirmation_t *confirmation,
                                      boot_confirmation_health_t *health)
{
    if (confirmation == 0 || confirmation->initialized == 0u || health == 0) {
        return ERR_INVALID_ARG;
    }
    *health = confirmation->health;
    return SYS_OK;
}
