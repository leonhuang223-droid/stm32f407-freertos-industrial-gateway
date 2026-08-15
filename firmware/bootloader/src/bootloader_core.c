#include "bootloader_core.h"

#include <string.h>

static bootloader_port_t boot_port;
static int bootloader_ready;

static int is_app_slot(app_slot_t slot)
{
    return slot == SLOT_A || slot == SLOT_B;
}

static status_t ensure_ready(void)
{
    return bootloader_ready ? SYS_OK : ERR_INVALID_ARG;
}

static int port_complete(const bootloader_port_t *port)
{
    return port != 0 &&
           port->minimal_init != 0 &&
           port->get_reset_reason != 0 &&
           port->validate_slot != 0 &&
           port->confirm_slot != 0 &&
           port->validate_staging != 0 &&
           port->program_inactive_slot != 0 &&
           port->jump_to_slot != 0 &&
           port->enter_maintenance != 0 &&
           port->scan_recovery != 0 &&
           port->metadata_store.read != 0 &&
           port->metadata_store.write != 0;
}

static void decision_init(bootloader_decision_t *decision)
{
    memset(decision, 0, sizeof(*decision));
    decision->action = BOOTLOADER_ACTION_NONE;
    decision->stage = BOOTLOADER_STAGE_RESET;
    decision->status = ERR_INVALID_ARG;
    decision->selected_slot = SLOT_NONE;
}

static status_t set_maintenance(bootloader_decision_t *decision, status_t reason,
                                const boot_metadata_t *metadata)
{
    decision->action = BOOTLOADER_ACTION_MAINTENANCE;
    decision->stage = BOOTLOADER_STAGE_MAINTENANCE;
    decision->status = reason;
    decision->selected_slot = SLOT_NONE;
    if (metadata != 0) {
        decision->metadata = *metadata;
    }
    return reason;
}

static status_t set_jump(bootloader_decision_t *decision, app_slot_t slot,
                         const boot_metadata_t *metadata)
{
    decision->action = BOOTLOADER_ACTION_JUMP_SLOT;
    decision->stage = BOOTLOADER_STAGE_JUMP_APP;
    decision->status = SYS_OK;
    decision->selected_slot = slot;
    decision->metadata = *metadata;
    return SYS_OK;
}

static status_t commit_metadata(const boot_metadata_t *current, app_slot_t current_copy_slot,
                                const boot_metadata_t *desired,
                                boot_metadata_t *out_committed,
                                app_slot_t *out_copy_slot)
{
    app_slot_t written_slot;
    status_t status;

    status = boot_meta_commit(&boot_port.metadata_store, current, current_copy_slot,
                              desired, out_committed, &written_slot);
    if (status == SYS_OK && out_copy_slot != 0) {
        *out_copy_slot = written_slot;
    }

    return status;
}

static status_t select_valid_slot(bootloader_decision_t *decision,
                                  const boot_metadata_t *metadata,
                                  app_slot_t primary_slot,
                                  app_slot_t fallback_slot)
{
    status_t status;

    decision->stage = BOOTLOADER_STAGE_VALIDATE_SLOT;
    status = bootloader_validate_slot(primary_slot);
    if (status == SYS_OK) {
        return set_jump(decision, primary_slot, metadata);
    }

    if (is_app_slot(fallback_slot) && fallback_slot != primary_slot) {
        status_t fallback_status = bootloader_validate_slot(fallback_slot);
        if (fallback_status == SYS_OK) {
            return set_jump(decision, fallback_slot, metadata);
        }
    }

    return set_maintenance(decision, status, metadata);
}

static status_t recover_confirmed_previous(
    bootloader_decision_t *decision,
    const boot_metadata_t *metadata,
    app_slot_t copy_slot,
    status_t failure_status)
{
    boot_meta_recovery_scan_t scan;
    boot_metadata_t recovered;
    boot_metadata_t committed;
    status_t status;

    if (!is_app_slot(metadata->previous_slot) ||
        metadata->previous_slot == metadata->active_slot) {
        return set_maintenance(decision, failure_status, metadata);
    }

    memset(&scan, 0, sizeof(scan));
    memset(&recovered, 0, sizeof(recovered));
    decision->stage = BOOTLOADER_STAGE_RECOVERY_SCAN;
    status = boot_port.scan_recovery(boot_port.context, &scan);
    if (status != SYS_OK) {
        return set_maintenance(decision, status, metadata);
    }
    status = boot_meta_recover_by_scan(&scan, &recovered);
    if (status != SYS_OK ||
        recovered.boot_state != BOOT_STATE_NORMAL ||
        recovered.active_slot != metadata->previous_slot) {
        return set_maintenance(decision, failure_status, metadata);
    }

    decision->stage = BOOTLOADER_STAGE_VALIDATE_SLOT;
    status = bootloader_validate_slot(recovered.active_slot);
    if (status != SYS_OK) {
        return set_maintenance(decision, status, metadata);
    }
    status = commit_metadata(
        metadata, copy_slot, &recovered, &committed, 0);
    if (status != SYS_OK) {
        return set_maintenance(decision, status, &recovered);
    }
    return set_jump(decision, committed.active_slot, &committed);
}

static status_t confirm_and_select_normal(
    bootloader_decision_t *decision,
    const boot_metadata_t *metadata,
    app_slot_t copy_slot)
{
    status_t status;

    decision->stage = BOOTLOADER_STAGE_VALIDATE_SLOT;
    status = bootloader_validate_slot(metadata->active_slot);
    if (status != SYS_OK) {
        return recover_confirmed_previous(
            decision, metadata, copy_slot, status);
    }

    status = boot_port.confirm_slot(
        boot_port.context, metadata->active_slot);
    if (status != SYS_OK) {
        return recover_confirmed_previous(
            decision, metadata, copy_slot, status);
    }
    return set_jump(decision, metadata->active_slot, metadata);
}

static status_t mark_rollback_commit_and_select(bootloader_decision_t *decision,
                                                const boot_metadata_t *metadata,
                                                app_slot_t copy_slot,
                                                boot_rollback_reason_t reason)
{
    boot_metadata_t rollback;
    boot_metadata_t committed;
    status_t status;

    status = boot_meta_mark_rollback(metadata, (uint32_t)reason, &rollback);
    if (status != SYS_OK) {
        return set_maintenance(decision, status, metadata);
    }

    status = commit_metadata(metadata, copy_slot, &rollback, &committed, 0);
    if (status != SYS_OK) {
        return set_maintenance(decision, status, &rollback);
    }

    return select_valid_slot(decision, &committed, committed.active_slot, committed.previous_slot);
}

static status_t handle_pending(bootloader_decision_t *decision,
                               const boot_metadata_t *metadata,
                               app_slot_t copy_slot)
{
    boot_metadata_t trial;
    boot_metadata_t committed;
    app_slot_t committed_copy_slot = SLOT_NONE;
    status_t status;

    decision->stage = BOOTLOADER_STAGE_VERIFY_STAGING;
    status = boot_port.validate_staging(boot_port.context, metadata);
    if (status != SYS_OK) {
        return mark_rollback_commit_and_select(decision, metadata, copy_slot,
                                               BOOT_ROLLBACK_STAGING_INVALID);
    }

    decision->stage = BOOTLOADER_STAGE_PROGRAM_INACTIVE;
    status = bootloader_program_inactive_slot(metadata);
    if (status != SYS_OK) {
        return mark_rollback_commit_and_select(decision, metadata, copy_slot,
                                               BOOT_ROLLBACK_WRITE_FAILED);
    }

    decision->stage = BOOTLOADER_STAGE_SET_TRIAL;
    status = boot_meta_request_trial(metadata, &trial);
    if (status != SYS_OK) {
        return set_maintenance(decision, status, metadata);
    }

    status = commit_metadata(metadata, copy_slot, &trial, &committed, &committed_copy_slot);
    if (status != SYS_OK) {
        return set_maintenance(decision, status, &trial);
    }

    status = bootloader_validate_slot(committed.pending_slot);
    if (status != SYS_OK) {
        return mark_rollback_commit_and_select(decision, &committed,
                                               committed_copy_slot,
                                               BOOT_ROLLBACK_WRITE_FAILED);
    }

    return set_jump(decision, committed.pending_slot, &committed);
}

static status_t handle_trial(bootloader_decision_t *decision,
                             const boot_metadata_t *metadata,
                             app_slot_t copy_slot)
{
    boot_metadata_t observed;
    boot_metadata_t retry;
    boot_metadata_t committed;
    boot_reset_reason_t reset_reason;
    status_t status;

    decision->stage = BOOTLOADER_STAGE_TRIAL_CHECK;
    status = boot_port.get_reset_reason(boot_port.context, &reset_reason);
    if (status != SYS_OK) {
        return set_maintenance(decision, status, metadata);
    }

    observed = *metadata;
    observed.last_reset_reason = (uint32_t)reset_reason;
    status = boot_meta_refresh_crc(&observed);
    if (status != SYS_OK) {
        return set_maintenance(decision, status, metadata);
    }

    if (reset_reason == BOOT_RESET_REASON_IWDG ||
        reset_reason == BOOT_RESET_REASON_WWDG) {
        return mark_rollback_commit_and_select(
            decision, &observed, copy_slot, BOOT_ROLLBACK_WATCHDOG);
    }
    if (reset_reason == BOOT_RESET_REASON_HARDFAULT) {
        return mark_rollback_commit_and_select(
            decision, &observed, copy_slot, BOOT_ROLLBACK_HARDFAULT);
    }

    status = boot_meta_request_trial(&observed, &retry);
    if (status == SYS_OK) {
        status = commit_metadata(&observed, copy_slot, &retry, &committed, 0);
        if (status != SYS_OK) {
            return set_maintenance(decision, status, &retry);
        }

        return select_valid_slot(decision, &committed, committed.pending_slot,
                                 committed.previous_slot);
    }

    if (status == ERR_TIMEOUT) {
        return mark_rollback_commit_and_select(decision, &observed, copy_slot,
                                               BOOT_ROLLBACK_ATTEMPT_EXCEEDED);
    }

    return set_maintenance(decision, status, &observed);
}

static status_t recover_metadata(bootloader_decision_t *decision, status_t load_status)
{
    boot_meta_recovery_scan_t scan;
    boot_metadata_t recovered;
    boot_metadata_t committed;
    status_t status;

    memset(&scan, 0, sizeof(scan));
    memset(&recovered, 0, sizeof(recovered));

    decision->stage = BOOTLOADER_STAGE_RECOVERY_SCAN;
    status = boot_port.scan_recovery(boot_port.context, &scan);
    if (status != SYS_OK) {
        return set_maintenance(decision, status, 0);
    }

    status = boot_meta_recover_by_scan(&scan, &recovered);
    if (status != SYS_OK) {
        return set_maintenance(decision, status, 0);
    }

    if (recovered.boot_state != BOOT_STATE_NORMAL) {
        decision->metadata = recovered;
        return set_maintenance(decision, ERR_METADATA_INVALID, &recovered);
    }

    status = commit_metadata(&recovered, SLOT_NONE, &recovered, &committed, 0);
    if (status != SYS_OK) {
        return set_maintenance(decision, status, &recovered);
    }

    (void)load_status;
    return select_valid_slot(decision, &committed, committed.active_slot, SLOT_NONE);
}

status_t bootloader_init(const bootloader_port_t *port)
{
    status_t status;

    bootloader_ready = 0;
    memset(&boot_port, 0, sizeof(boot_port));

    if (!port_complete(port)) {
        return ERR_INVALID_ARG;
    }

    boot_port = *port;
    status = boot_port.minimal_init(boot_port.context);
    if (status != SYS_OK) {
        memset(&boot_port, 0, sizeof(boot_port));
        return status;
    }

    bootloader_ready = 1;
    return SYS_OK;
}

status_t bootloader_select_slot(bootloader_decision_t *out_decision)
{
    boot_metadata_t metadata;
    app_slot_t copy_slot = SLOT_NONE;
    status_t status;

    if (out_decision == 0) {
        return ERR_INVALID_ARG;
    }

    decision_init(out_decision);
    status = ensure_ready();
    if (status != SYS_OK) {
        out_decision->status = status;
        return status;
    }

    out_decision->stage = BOOTLOADER_STAGE_LOAD_METADATA;
    status = boot_meta_load(&boot_port.metadata_store, &metadata, &copy_slot);
    if (status != SYS_OK) {
        return recover_metadata(out_decision, status);
    }

    out_decision->metadata = metadata;
    switch (metadata.boot_state) {
    case BOOT_STATE_NORMAL:
    case BOOT_STATE_ROLLBACK:
        return confirm_and_select_normal(
            out_decision, &metadata, copy_slot);

    case BOOT_STATE_PENDING:
        out_decision->stage = BOOTLOADER_STAGE_CHECK_PENDING;
        return handle_pending(out_decision, &metadata, copy_slot);

    case BOOT_STATE_TRIAL:
        return handle_trial(out_decision, &metadata, copy_slot);

    case BOOT_STATE_MAINTENANCE:
        return set_maintenance(out_decision, ERR_METADATA_INVALID, &metadata);

    default:
        return set_maintenance(out_decision, ERR_METADATA_INVALID, &metadata);
    }
}

status_t bootloader_program_inactive_slot(const boot_metadata_t *metadata)
{
    status_t status = ensure_ready();

    if (status != SYS_OK) {
        return status;
    }
    if (metadata == 0) {
        return ERR_INVALID_ARG;
    }

    status = boot_meta_validate(metadata);
    if (status != SYS_OK) {
        return status;
    }
    if (metadata->boot_state != BOOT_STATE_PENDING ||
        !is_app_slot(metadata->active_slot) ||
        !is_app_slot(metadata->pending_slot) ||
        metadata->active_slot == metadata->pending_slot) {
        return ERR_METADATA_INVALID;
    }

    return boot_port.program_inactive_slot(boot_port.context, metadata);
}

status_t bootloader_validate_slot(app_slot_t slot)
{
    status_t status = ensure_ready();

    if (status != SYS_OK) {
        return status;
    }
    if (!is_app_slot(slot)) {
        return ERR_SLOT_MISMATCH;
    }

    return boot_port.validate_slot(boot_port.context, slot);
}

status_t bootloader_jump_to_slot(app_slot_t slot)
{
    status_t status = bootloader_validate_slot(slot);

    if (status != SYS_OK) {
        return status;
    }

    return boot_port.jump_to_slot(boot_port.context, slot);
}

status_t bootloader_enter_maintenance(status_t reason)
{
    status_t status = ensure_ready();

    if (status != SYS_OK) {
        return status;
    }

    return boot_port.enter_maintenance(boot_port.context, reason);
}

static status_t execute_maintenance(
    bootloader_decision_t *decision, status_t reason)
{
    boot_metadata_t metadata = decision->metadata;
    status_t maintenance_status;

    (void)set_maintenance(decision, reason, &metadata);
    maintenance_status = bootloader_enter_maintenance(reason);
    return maintenance_status == SYS_OK ? reason : maintenance_status;
}

status_t bootloader_execute(int force_maintenance,
                            bootloader_decision_t *out_decision)
{
    status_t status;

    if (out_decision == 0) {
        return ERR_INVALID_ARG;
    }
    decision_init(out_decision);
    status = ensure_ready();
    if (status != SYS_OK) {
        out_decision->status = status;
        return status;
    }

    if (force_maintenance != 0) {
        return execute_maintenance(out_decision, ERR_RESET_REQUIRED);
    }

    status = bootloader_select_slot(out_decision);
    if (out_decision->action == BOOTLOADER_ACTION_JUMP_SLOT) {
        status = bootloader_jump_to_slot(out_decision->selected_slot);
        if (status == SYS_OK) {
            status = ERR_RESET_REQUIRED;
        }
        return execute_maintenance(out_decision, status);
    }
    if (out_decision->action == BOOTLOADER_ACTION_MAINTENANCE) {
        return execute_maintenance(out_decision, out_decision->status);
    }
    return execute_maintenance(
        out_decision, status == SYS_OK ? ERR_METADATA_INVALID : status);
}
