#include "boot_metadata.h"

#include "crc32.h"

#include <string.h>

static int is_app_slot(app_slot_t slot)
{
    return slot == SLOT_A || slot == SLOT_B;
}

static int is_slot_or_none(app_slot_t slot)
{
    return is_app_slot(slot) || slot == SLOT_NONE;
}

static int is_valid_state(boot_state_t state)
{
    return state == BOOT_STATE_NORMAL ||
           state == BOOT_STATE_PENDING ||
           state == BOOT_STATE_TRIAL ||
           state == BOOT_STATE_ROLLBACK ||
           state == BOOT_STATE_MAINTENANCE;
}

static int version_input_valid(const char *version)
{
    size_t i;

    if (version == 0 || version[0] == '\0') {
        return 0;
    }

    for (i = 0u; i < BOOT_METADATA_VERSION_LEN; ++i) {
        if (version[i] == '\0') {
            return 1;
        }
    }

    return 0;
}

static int version_array_valid(const char version[BOOT_METADATA_VERSION_LEN], int require_nonempty)
{
    size_t i;

    if (require_nonempty && version[0] == '\0') {
        return 0;
    }

    for (i = 0u; i < BOOT_METADATA_VERSION_LEN; ++i) {
        if (version[i] == '\0') {
            return 1;
        }
    }

    return 0;
}

static int version_array_empty(const char version[BOOT_METADATA_VERSION_LEN])
{
    size_t i;

    for (i = 0u; i < BOOT_METADATA_VERSION_LEN; ++i) {
        if (version[i] != '\0') {
            return 0;
        }
    }

    return 1;
}

static void version_copy(char out[BOOT_METADATA_VERSION_LEN], const char *version)
{
    size_t i;

    memset(out, 0, BOOT_METADATA_VERSION_LEN);
    for (i = 0u; i < BOOT_METADATA_VERSION_LEN - 1u && version[i] != '\0'; ++i) {
        out[i] = version[i];
    }
}

static int sha256_nonzero(const uint8_t sha[IMAGE_SHA256_LEN])
{
    size_t i;

    for (i = 0u; i < IMAGE_SHA256_LEN; ++i) {
        if (sha[i] != 0u) {
            return 1;
        }
    }

    return 0;
}

static int sha256_zero(const uint8_t sha[IMAGE_SHA256_LEN])
{
    return !sha256_nonzero(sha);
}

static status_t crc_update_u8(crc32_context_t *ctx, uint8_t value)
{
    return crc32_update(ctx, &value, 1u);
}

static status_t crc_update_u16_le(crc32_context_t *ctx, uint16_t value)
{
    uint8_t bytes[2];

    bytes[0] = (uint8_t)(value & 0xFFu);
    bytes[1] = (uint8_t)((value >> 8) & 0xFFu);
    return crc32_update(ctx, bytes, sizeof(bytes));
}

static status_t crc_update_u32_le(crc32_context_t *ctx, uint32_t value)
{
    uint8_t bytes[4];

    bytes[0] = (uint8_t)(value & 0xFFu);
    bytes[1] = (uint8_t)((value >> 8) & 0xFFu);
    bytes[2] = (uint8_t)((value >> 16) & 0xFFu);
    bytes[3] = (uint8_t)((value >> 24) & 0xFFu);
    return crc32_update(ctx, bytes, sizeof(bytes));
}

static status_t metadata_crc32_compute(const boot_metadata_t *metadata, uint32_t *out_crc)
{
    crc32_context_t ctx;
    status_t status;

    if (metadata == 0 || out_crc == 0) {
        return ERR_INVALID_ARG;
    }

    status = crc32_init(&ctx);
    if (status != SYS_OK) {
        return status;
    }

#define UPDATE_OR_RETURN(expr) do { \
    status = (expr); \
    if (status != SYS_OK) { \
        return status; \
    } \
} while (0)

    UPDATE_OR_RETURN(crc_update_u32_le(&ctx, metadata->metadata_magic));
    UPDATE_OR_RETURN(crc_update_u16_le(&ctx, metadata->metadata_version));
    UPDATE_OR_RETURN(crc_update_u32_le(&ctx, metadata->sequence));
    UPDATE_OR_RETURN(crc_update_u32_le(&ctx, (uint32_t)metadata->active_slot));
    UPDATE_OR_RETURN(crc_update_u32_le(&ctx, (uint32_t)metadata->previous_slot));
    UPDATE_OR_RETURN(crc_update_u32_le(&ctx, (uint32_t)metadata->pending_slot));
    UPDATE_OR_RETURN(crc_update_u32_le(&ctx, (uint32_t)metadata->boot_state));
    UPDATE_OR_RETURN(crc_update_u8(&ctx, metadata->boot_attempt));
    UPDATE_OR_RETURN(crc_update_u8(&ctx, metadata->max_boot_attempt));
    UPDATE_OR_RETURN(crc_update_u8(&ctx, metadata->boot_ok));
    UPDATE_OR_RETURN(crc32_update(&ctx, (const uint8_t *)metadata->active_version,
                                  sizeof(metadata->active_version)));
    UPDATE_OR_RETURN(crc32_update(&ctx, (const uint8_t *)metadata->pending_version,
                                  sizeof(metadata->pending_version)));
    UPDATE_OR_RETURN(crc_update_u32_le(&ctx, metadata->last_reset_reason));
    UPDATE_OR_RETURN(crc_update_u32_le(&ctx, metadata->rollback_reason));
    UPDATE_OR_RETURN(crc_update_u32_le(&ctx, metadata->staging_image_crc32));
    UPDATE_OR_RETURN(crc32_update(&ctx, metadata->staging_image_sha256,
                                  sizeof(metadata->staging_image_sha256)));
    UPDATE_OR_RETURN(crc_update_u32_le(&ctx, 0u));

#undef UPDATE_OR_RETURN

    return crc32_final(&ctx, out_crc);
}

static status_t validate_normal_like(const boot_metadata_t *metadata)
{
    if (!is_app_slot(metadata->active_slot) ||
        !is_slot_or_none(metadata->previous_slot) ||
        metadata->pending_slot != SLOT_NONE ||
        metadata->boot_attempt != 0u ||
        metadata->boot_ok != 1u ||
        !version_array_valid(metadata->active_version, 1) ||
        !version_array_empty(metadata->pending_version) ||
        metadata->staging_image_crc32 != 0u ||
        !sha256_zero(metadata->staging_image_sha256)) {
        return ERR_METADATA_INVALID;
    }

    return SYS_OK;
}

static status_t validate_state_consistency(const boot_metadata_t *metadata)
{
    if (metadata->max_boot_attempt == 0u ||
        (metadata->boot_ok != 0u && metadata->boot_ok != 1u) ||
        !is_slot_or_none(metadata->active_slot) ||
        !is_slot_or_none(metadata->previous_slot) ||
        !is_slot_or_none(metadata->pending_slot) ||
        !is_valid_state(metadata->boot_state)) {
        return ERR_METADATA_INVALID;
    }

    switch (metadata->boot_state) {
    case BOOT_STATE_NORMAL:
        return validate_normal_like(metadata);

    case BOOT_STATE_PENDING:
        if (!is_app_slot(metadata->active_slot) ||
            !is_slot_or_none(metadata->previous_slot) ||
            !is_app_slot(metadata->pending_slot) ||
            metadata->pending_slot == metadata->active_slot ||
            metadata->boot_attempt != 0u ||
            metadata->boot_ok != 1u ||
            !version_array_valid(metadata->active_version, 1) ||
            !version_array_valid(metadata->pending_version, 1) ||
            !sha256_nonzero(metadata->staging_image_sha256) ||
            metadata->rollback_reason != BOOT_ROLLBACK_NONE) {
            return ERR_METADATA_INVALID;
        }
        return SYS_OK;

    case BOOT_STATE_TRIAL:
        if (!is_app_slot(metadata->active_slot) ||
            !is_app_slot(metadata->previous_slot) ||
            !is_app_slot(metadata->pending_slot) ||
            metadata->active_slot != metadata->previous_slot ||
            metadata->pending_slot == metadata->previous_slot ||
            metadata->boot_attempt == 0u ||
            metadata->boot_attempt > metadata->max_boot_attempt ||
            metadata->boot_ok != 0u ||
            !version_array_valid(metadata->active_version, 1) ||
            !version_array_valid(metadata->pending_version, 1) ||
            !sha256_nonzero(metadata->staging_image_sha256)) {
            return ERR_METADATA_INVALID;
        }
        return SYS_OK;

    case BOOT_STATE_ROLLBACK:
        if (!is_app_slot(metadata->active_slot) ||
            !is_app_slot(metadata->previous_slot) ||
            metadata->pending_slot != SLOT_NONE ||
            metadata->boot_attempt != 0u ||
            metadata->boot_ok != 1u ||
            metadata->rollback_reason == BOOT_ROLLBACK_NONE ||
            !version_array_valid(metadata->active_version, 1) ||
            !version_array_empty(metadata->pending_version) ||
            metadata->staging_image_crc32 != 0u ||
            !sha256_zero(metadata->staging_image_sha256)) {
            return ERR_METADATA_INVALID;
        }
        return SYS_OK;

    case BOOT_STATE_MAINTENANCE:
        if (metadata->active_slot != SLOT_NONE ||
            metadata->previous_slot != SLOT_NONE ||
            metadata->pending_slot != SLOT_NONE ||
            metadata->boot_attempt != 0u ||
            metadata->boot_ok != 0u ||
            !version_array_empty(metadata->active_version) ||
            !version_array_empty(metadata->pending_version) ||
            metadata->staging_image_crc32 != 0u ||
            !sha256_zero(metadata->staging_image_sha256)) {
            return ERR_METADATA_INVALID;
        }
        return SYS_OK;

    default:
        return ERR_METADATA_INVALID;
    }
}

static void init_maintenance(boot_metadata_t *metadata)
{
    memset(metadata, 0, sizeof(*metadata));
    metadata->metadata_magic = BOOT_METADATA_MAGIC;
    metadata->metadata_version = (uint16_t)BOOT_METADATA_VERSION;
    metadata->active_slot = SLOT_NONE;
    metadata->previous_slot = SLOT_NONE;
    metadata->pending_slot = SLOT_NONE;
    metadata->boot_state = BOOT_STATE_MAINTENANCE;
    metadata->max_boot_attempt = BOOT_METADATA_DEFAULT_MAX_ATTEMPT;
    metadata->boot_ok = 0u;
    metadata->rollback_reason = BOOT_ROLLBACK_RECOVER_META_LOST;
    (void)boot_meta_refresh_crc(metadata);
}

status_t boot_meta_init_default(boot_metadata_t *metadata, app_slot_t active_slot,
                                const char *active_version)
{
    if (metadata == 0 || !is_app_slot(active_slot) || !version_input_valid(active_version)) {
        return ERR_INVALID_ARG;
    }

    memset(metadata, 0, sizeof(*metadata));
    metadata->metadata_magic = BOOT_METADATA_MAGIC;
    metadata->metadata_version = (uint16_t)BOOT_METADATA_VERSION;
    metadata->sequence = 0u;
    metadata->active_slot = active_slot;
    metadata->previous_slot = SLOT_NONE;
    metadata->pending_slot = SLOT_NONE;
    metadata->boot_state = BOOT_STATE_NORMAL;
    metadata->boot_attempt = 0u;
    metadata->max_boot_attempt = BOOT_METADATA_DEFAULT_MAX_ATTEMPT;
    metadata->boot_ok = 1u;
    version_copy(metadata->active_version, active_version);
    metadata->rollback_reason = BOOT_ROLLBACK_NONE;

    return boot_meta_refresh_crc(metadata);
}

status_t boot_meta_validate(const boot_metadata_t *metadata)
{
    uint32_t crc;
    status_t status;

    if (metadata == 0) {
        return ERR_INVALID_ARG;
    }

    if (metadata->metadata_magic != BOOT_METADATA_MAGIC ||
        metadata->metadata_version != (uint16_t)BOOT_METADATA_VERSION) {
        return ERR_METADATA_INVALID;
    }

    status = metadata_crc32_compute(metadata, &crc);
    if (status != SYS_OK) {
        return status;
    }
    if (crc != metadata->metadata_crc32) {
        return ERR_CRC;
    }

    return validate_state_consistency(metadata);
}

status_t boot_meta_refresh_crc(boot_metadata_t *metadata)
{
    uint32_t crc;
    status_t status;

    if (metadata == 0) {
        return ERR_INVALID_ARG;
    }

    status = metadata_crc32_compute(metadata, &crc);
    if (status != SYS_OK) {
        return status;
    }

    metadata->metadata_crc32 = crc;
    return SYS_OK;
}

status_t boot_meta_load(const boot_meta_store_t *store, boot_metadata_t *out_metadata,
                        app_slot_t *out_copy_slot)
{
    boot_metadata_t copy_a;
    boot_metadata_t copy_b;
    status_t read_a;
    status_t read_b;
    status_t valid_a;
    status_t valid_b;
    int a_ok;
    int b_ok;

    if (store == 0 || store->read == 0 || out_metadata == 0 || out_copy_slot == 0) {
        return ERR_INVALID_ARG;
    }

    *out_copy_slot = SLOT_NONE;
    memset(&copy_a, 0, sizeof(copy_a));
    memset(&copy_b, 0, sizeof(copy_b));

    read_a = store->read(store->context, SLOT_A, &copy_a);
    read_b = store->read(store->context, SLOT_B, &copy_b);
    valid_a = (read_a == SYS_OK) ? boot_meta_validate(&copy_a) : read_a;
    valid_b = (read_b == SYS_OK) ? boot_meta_validate(&copy_b) : read_b;
    a_ok = (valid_a == SYS_OK);
    b_ok = (valid_b == SYS_OK);

    if (a_ok && b_ok) {
        if (copy_b.sequence > copy_a.sequence) {
            *out_metadata = copy_b;
            *out_copy_slot = SLOT_B;
        } else {
            *out_metadata = copy_a;
            *out_copy_slot = SLOT_A;
        }
        return SYS_OK;
    }

    if (a_ok) {
        *out_metadata = copy_a;
        *out_copy_slot = SLOT_A;
        return SYS_OK;
    }

    if (b_ok) {
        *out_metadata = copy_b;
        *out_copy_slot = SLOT_B;
        return SYS_OK;
    }

    if (read_a != SYS_OK && read_b != SYS_OK) {
        return read_a;
    }

    return ERR_METADATA_INVALID;
}

status_t boot_meta_commit(const boot_meta_store_t *store, const boot_metadata_t *current,
                          app_slot_t current_copy_slot, const boot_metadata_t *desired,
                          boot_metadata_t *out_committed, app_slot_t *out_copy_slot)
{
    app_slot_t target_slot;
    boot_metadata_t committed;
    boot_metadata_t readback;
    status_t status;

    if (store == 0 || store->read == 0 || store->write == 0 ||
        current == 0 || desired == 0 ||
        out_committed == 0 || out_copy_slot == 0) {
        return ERR_INVALID_ARG;
    }

    *out_copy_slot = SLOT_NONE;

    if (current_copy_slot == SLOT_A) {
        target_slot = SLOT_B;
    } else if (current_copy_slot == SLOT_B || current_copy_slot == SLOT_NONE) {
        target_slot = SLOT_A;
    } else {
        return ERR_INVALID_ARG;
    }

    status = boot_meta_validate(current);
    if (status != SYS_OK) {
        return status;
    }

    if (current->sequence == 0xFFFFFFFFu) {
        return ERR_TIMEOUT;
    }

    committed = *desired;
    committed.sequence = current->sequence + 1u;
    status = boot_meta_refresh_crc(&committed);
    if (status != SYS_OK) {
        return status;
    }

    status = boot_meta_validate(&committed);
    if (status != SYS_OK) {
        return status;
    }

    status = store->write(store->context, target_slot, &committed);
    if (status != SYS_OK) {
        return status;
    }

    memset(&readback, 0, sizeof(readback));
    status = store->read(store->context, target_slot, &readback);
    if (status != SYS_OK) {
        return status;
    }
    status = boot_meta_validate(&readback);
    if (status != SYS_OK) {
        return status;
    }
    if (readback.sequence != committed.sequence ||
        readback.metadata_crc32 != committed.metadata_crc32) {
        return ERR_FLASH_VERIFY;
    }

    *out_committed = readback;
    *out_copy_slot = target_slot;
    return SYS_OK;
}

status_t boot_meta_request_staging(const boot_metadata_t *current, app_slot_t pending_slot,
                                   const char *pending_version, uint32_t staging_crc32,
                                   const uint8_t staging_sha256[IMAGE_SHA256_LEN],
                                   boot_metadata_t *out_metadata)
{
    boot_metadata_t next;
    status_t status;

    if (current == 0 || pending_version == 0 || staging_sha256 == 0 || out_metadata == 0) {
        return ERR_INVALID_ARG;
    }
    if (!is_app_slot(pending_slot)) {
        return ERR_INVALID_ARG;
    }
    if (!version_input_valid(pending_version)) {
        return ERR_INVALID_ARG;
    }
    if (!sha256_nonzero(staging_sha256)) {
        return ERR_SHA256;
    }

    status = boot_meta_validate(current);
    if (status != SYS_OK) {
        return status;
    }
    if (current->boot_state != BOOT_STATE_NORMAL) {
        return ERR_METADATA_INVALID;
    }
    if (!is_app_slot(current->active_slot)) {
        return ERR_METADATA_INVALID;
    }
    if (pending_slot == current->active_slot) {
        return ERR_SLOT_MISMATCH;
    }

    next = *current;
    next.pending_slot = pending_slot;
    next.boot_state = BOOT_STATE_PENDING;
    next.boot_attempt = 0u;
    version_copy(next.pending_version, pending_version);
    next.rollback_reason = BOOT_ROLLBACK_NONE;
    next.staging_image_crc32 = staging_crc32;
    memcpy(next.staging_image_sha256, staging_sha256, IMAGE_SHA256_LEN);

    status = boot_meta_refresh_crc(&next);
    if (status != SYS_OK) {
        return status;
    }
    status = boot_meta_validate(&next);
    if (status != SYS_OK) {
        return status;
    }

    *out_metadata = next;
    return SYS_OK;
}

status_t boot_meta_request_trial(const boot_metadata_t *current, boot_metadata_t *out_metadata)
{
    boot_metadata_t next;
    status_t status;

    if (current == 0 || out_metadata == 0) {
        return ERR_INVALID_ARG;
    }

    status = boot_meta_validate(current);
    if (status != SYS_OK) {
        return status;
    }

    next = *current;
    if (current->boot_state == BOOT_STATE_PENDING) {
        next.previous_slot = current->active_slot;
        next.boot_state = BOOT_STATE_TRIAL;
        next.boot_attempt = 1u;
        next.boot_ok = 0u;
    } else if (current->boot_state == BOOT_STATE_TRIAL) {
        if (current->boot_attempt >= current->max_boot_attempt) {
            return ERR_TIMEOUT;
        }
        next.boot_attempt = (uint8_t)(current->boot_attempt + 1u);
        next.boot_ok = 0u;
    } else {
        return ERR_METADATA_INVALID;
    }

    status = boot_meta_refresh_crc(&next);
    if (status != SYS_OK) {
        return status;
    }
    status = boot_meta_validate(&next);
    if (status != SYS_OK) {
        return status;
    }

    *out_metadata = next;
    return SYS_OK;
}

status_t boot_meta_confirm_boot_ok(const boot_metadata_t *current, boot_metadata_t *out_metadata)
{
    boot_metadata_t next;
    status_t status;
    app_slot_t old_active;

    if (current == 0 || out_metadata == 0) {
        return ERR_INVALID_ARG;
    }

    status = boot_meta_validate(current);
    if (status != SYS_OK) {
        return status;
    }
    if (current->boot_state != BOOT_STATE_TRIAL || !is_app_slot(current->pending_slot)) {
        return ERR_METADATA_INVALID;
    }

    next = *current;
    old_active = current->active_slot;
    next.previous_slot = old_active;
    next.active_slot = current->pending_slot;
    next.pending_slot = SLOT_NONE;
    next.boot_state = BOOT_STATE_NORMAL;
    next.boot_attempt = 0u;
    next.boot_ok = 1u;
    version_copy(next.active_version, current->pending_version);
    memset(next.pending_version, 0, sizeof(next.pending_version));
    next.rollback_reason = BOOT_ROLLBACK_NONE;
    next.staging_image_crc32 = 0u;
    memset(next.staging_image_sha256, 0, sizeof(next.staging_image_sha256));

    status = boot_meta_refresh_crc(&next);
    if (status != SYS_OK) {
        return status;
    }
    status = boot_meta_validate(&next);
    if (status != SYS_OK) {
        return status;
    }

    *out_metadata = next;
    return SYS_OK;
}

status_t boot_meta_mark_rollback(const boot_metadata_t *current, uint32_t rollback_reason,
                                 boot_metadata_t *out_metadata)
{
    boot_metadata_t next;
    status_t status;

    if (current == 0 || out_metadata == 0) {
        return ERR_INVALID_ARG;
    }
    if (rollback_reason == BOOT_ROLLBACK_NONE) {
        return ERR_INVALID_ARG;
    }

    status = boot_meta_validate(current);
    if (status != SYS_OK) {
        return status;
    }
    next = *current;
    if (current->boot_state == BOOT_STATE_PENDING) {
        if (rollback_reason != BOOT_ROLLBACK_STAGING_INVALID &&
            rollback_reason != BOOT_ROLLBACK_WRITE_FAILED) {
            return ERR_INVALID_ARG;
        }
    } else if (current->boot_state == BOOT_STATE_TRIAL) {
        if (!is_app_slot(current->previous_slot)) {
            return ERR_METADATA_INVALID;
        }
        next.active_slot = current->previous_slot;
    } else {
        return ERR_METADATA_INVALID;
    }

    next.pending_slot = SLOT_NONE;
    next.boot_state = BOOT_STATE_NORMAL;
    next.boot_attempt = 0u;
    next.boot_ok = 1u;
    next.rollback_reason = rollback_reason;
    memset(next.pending_version, 0, sizeof(next.pending_version));
    next.staging_image_crc32 = 0u;
    memset(next.staging_image_sha256, 0, sizeof(next.staging_image_sha256));

    status = boot_meta_refresh_crc(&next);
    if (status != SYS_OK) {
        return status;
    }
    status = boot_meta_validate(&next);
    if (status != SYS_OK) {
        return status;
    }

    *out_metadata = next;
    return SYS_OK;
}

status_t boot_meta_recover_by_scan(const boot_meta_recovery_scan_t *scan,
                                   boot_metadata_t *out_metadata)
{
    app_slot_t confirmed_slot = SLOT_NONE;
    const char *confirmed_version = 0;
    unsigned int confirmed_count = 0u;
    status_t status;

    if (scan == 0 || out_metadata == 0) {
        return ERR_INVALID_ARG;
    }

    if (scan->app_a.image_valid != 0u && scan->app_a.image_state == IMAGE_STATE_CONFIRMED) {
        confirmed_slot = SLOT_A;
        confirmed_version = scan->app_a.version;
        confirmed_count++;
    }
    if (scan->app_b.image_valid != 0u && scan->app_b.image_state == IMAGE_STATE_CONFIRMED) {
        confirmed_slot = SLOT_B;
        confirmed_version = scan->app_b.version;
        confirmed_count++;
    }

    if (confirmed_count == 1u) {
        status = boot_meta_init_default(out_metadata, confirmed_slot, confirmed_version);
        if (status == SYS_OK) {
            out_metadata->rollback_reason = BOOT_ROLLBACK_RECOVER_META_LOST;
            return boot_meta_refresh_crc(out_metadata);
        }
    }

    init_maintenance(out_metadata);
    return SYS_OK;
}
