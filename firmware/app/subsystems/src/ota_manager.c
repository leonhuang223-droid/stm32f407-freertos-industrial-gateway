#include "ota_manager.h"

#include <string.h>

static void write_u16_le(uint8_t *out, uint16_t value)
{
    out[0] = (uint8_t)value;
    out[1] = (uint8_t)(value >> 8u);
}

static void write_u32_le(uint8_t *out, uint32_t value)
{
    out[0] = (uint8_t)value;
    out[1] = (uint8_t)(value >> 8u);
    out[2] = (uint8_t)(value >> 16u);
    out[3] = (uint8_t)(value >> 24u);
}

status_t ota_metadata_encode(const ota_metadata_record_t *record,
                             uint8_t *buffer,
                             size_t capacity)
{
    if (record == 0 || buffer == 0 ||
        capacity < OTA_STAGING_METADATA_WIRE_SIZE) {
        return ERR_INVALID_ARG;
    }
    memset(buffer, 0, OTA_STAGING_METADATA_WIRE_SIZE);
    write_u32_le(&buffer[0], record->magic);
    write_u16_le(&buffer[4], record->version);
    write_u16_le(&buffer[6], record->record_size);
    write_u32_le(&buffer[8], record->target_slot);
    write_u32_le(&buffer[12], record->image_size);
    write_u32_le(&buffer[16], record->downloaded_size);
    write_u32_le(&buffer[20], record->image_crc32);
    memcpy(&buffer[24], record->image_sha256, IMAGE_SHA256_LEN);
    memcpy(&buffer[56], record->image_version, MANIFEST_VERSION_LEN);
    memcpy(&buffer[72], record->download_url, MANIFEST_DOWNLOAD_URL_LEN);
    write_u32_le(&buffer[OTA_STAGING_METADATA_CRC_OFFSET],
                 record->record_crc32);
    return SYS_OK;
}

static int port_complete(const ota_manager_port_t *port)
{
    return port != 0 && port->fetch_manifest != 0 && port->http_open != 0 &&
           port->http_read != 0 && port->http_close != 0 &&
           port->staging_begin != 0 && port->staging_write != 0 &&
           port->metadata_load != 0 && port->metadata_commit != 0 &&
           port->ota_metadata_commit != 0 && port->enter_critical != 0 &&
           port->exit_critical != 0;
}

static void enter_critical(ota_manager_t *manager)
{
    manager->port.enter_critical(manager->port.context);
}

static void exit_critical(ota_manager_t *manager)
{
    manager->port.exit_critical(manager->port.context);
}

static int abort_requested(ota_manager_t *manager)
{
    int requested;

    enter_critical(manager);
    requested = manager->abort_requested != 0u;
    exit_critical(manager);
    return requested;
}

static status_t copy_text(char *out, size_t capacity, const char *text)
{
    size_t length;

    if (out == 0 || capacity == 0u || text == 0) {
        return ERR_INVALID_ARG;
    }
    length = strlen(text);
    if (length == 0u || length >= capacity) {
        return ERR_INVALID_ARG;
    }
    memcpy(out, text, length + 1u);
    return SYS_OK;
}

static status_t set_failed(ota_manager_t *manager, status_t error)
{
    enter_critical(manager);
    if (manager->abort_requested != 0u && error != ERR_OTA_ABORTED) {
        error = ERR_OTA_ABORTED;
    }
    manager->status.state = OTA_FAILED;
    manager->status.last_error = error;
    manager->commit_in_progress = 0u;
    exit_critical(manager);
    return error;
}

static status_t close_connection(ota_manager_t *manager)
{
    status_t status;

    if (manager->connection_open == 0u) {
        return SYS_OK;
    }
    status = manager->port.http_close(manager->port.context);
    if (status == SYS_OK) {
        manager->connection_open = 0u;
    }
    return status;
}

static status_t validate_normal_metadata(const boot_metadata_t *metadata)
{
    status_t status = boot_meta_validate(metadata);

    if (status != SYS_OK) {
        return status;
    }
    if (metadata->boot_state != BOOT_STATE_NORMAL ||
        (metadata->active_slot != SLOT_A && metadata->active_slot != SLOT_B) ||
        metadata->pending_slot != SLOT_NONE) {
        return ERR_METADATA_INVALID;
    }
    return SYS_OK;
}

static void reset_transfer_status(ota_manager_t *manager)
{
    manager->status.bytes_downloaded = 0u;
    manager->status.total_bytes = manager->manifest.image_size;
    manager->status.chunk_count = 0u;
    manager->status.expected_crc32 = manager->manifest.crc32;
    manager->status.actual_crc32 = 0u;
    memcpy(manager->status.expected_sha256,
           manager->manifest.sha256,
           IMAGE_SHA256_LEN);
    memset(manager->status.actual_sha256,
           0,
           sizeof(manager->status.actual_sha256));
    manager->status.crc_verified = 0u;
    manager->status.sha256_verified = 0u;
}

status_t ota_manager_construct(ota_manager_t *manager,
                               const ota_manager_port_t *port,
                               const ota_manager_config_t *config)
{
    status_t status;

    if (manager == 0 || !port_complete(port) || config == 0) {
        return ERR_INVALID_ARG;
    }
    memset(manager, 0, sizeof(*manager));
    manager->status.state = OTA_IDLE;
    manager->status.last_error = SYS_OK;
    manager->status.active_slot = SLOT_NONE;
    manager->status.target_slot = SLOT_NONE;
    status = copy_text(manager->manifest_url,
                       sizeof(manager->manifest_url),
                       config->manifest_url);
    if (status == SYS_OK) {
        status = copy_text(
            manager->target_id, sizeof(manager->target_id), config->target_id);
    }
    if (status == SYS_OK) {
        status = copy_text(manager->current_version,
                           sizeof(manager->current_version),
                           config->current_version);
    }
    if (status == SYS_OK) {
        status = copy_text(manager->current_bootloader_version,
                           sizeof(manager->current_bootloader_version),
                           config->current_bootloader_version);
    }
    if (status != SYS_OK) {
        return status;
    }
    manager->port = *port;
    manager->initialized = 1u;
    return SYS_OK;
}

static status_t validate_manifest_metadata(ota_manager_t *manager,
                                           boot_metadata_t *metadata)
{
    manifest_validate_context_t validate_context;
    app_slot_t copy_slot = SLOT_NONE;
    status_t status;

    validate_context.target_id = manager->target_id;
    validate_context.current_version = manager->current_version;
    validate_context.current_bootloader_version =
        manager->current_bootloader_version;
    status = manifest_validate(&manager->manifest, &validate_context);
    if (status != SYS_OK) {
        return set_failed(manager, status);
    }
    status = manager->port.metadata_load(
        manager->port.context, metadata, &copy_slot);
    if (status != SYS_OK) {
        return set_failed(manager, status);
    }
    status = validate_normal_metadata(metadata);
    if (status != SYS_OK) {
        return set_failed(manager, status);
    }
    if ((app_slot_t)manager->manifest.target_slot == metadata->active_slot) {
        return set_failed(manager, ERR_SLOT_MISMATCH);
    }

    return SYS_OK;
}

status_t ota_manager_check(ota_manager_t *manager)
{
    boot_metadata_t metadata;
    size_t manifest_length = 0u;
    status_t status;

    if (manager == 0 || manager->initialized == 0u) {
        return ERR_INVALID_ARG;
    }
    enter_critical(manager);
    if (manager->status.state != OTA_IDLE &&
        manager->status.state != OTA_FAILED) {
        exit_critical(manager);
        return ERR_INVALID_ARG;
    }
    memset(&manager->status, 0, sizeof(manager->status));
    manager->status.state = OTA_CHECKING;
    manager->status.active_slot = SLOT_NONE;
    manager->status.target_slot = SLOT_NONE;
    manager->abort_requested = 0u;
    manager->commit_in_progress = 0u;
    exit_critical(manager);
    status = close_connection(manager);
    if (status != SYS_OK) {
        return set_failed(manager, status);
    }
    memset(&manager->manifest, 0, sizeof(manager->manifest));
    memset(manager->manifest_buffer, 0, sizeof(manager->manifest_buffer));

    status = manager->port.fetch_manifest(manager->port.context,
                                          manager->manifest_url,
                                          manager->manifest_buffer,
                                          sizeof(manager->manifest_buffer) - 1u,
                                          &manifest_length);
    if (abort_requested(manager)) {
        return set_failed(manager, ERR_OTA_ABORTED);
    }
    if (status != SYS_OK) {
        return set_failed(manager, status);
    }
    if (manifest_length == 0u ||
        manifest_length >= sizeof(manager->manifest_buffer)) {
        return set_failed(manager, ERR_IMAGE_INVALID);
    }
    manager->manifest_buffer[manifest_length] = '\0';
    status = manifest_parse_json((const char *)manager->manifest_buffer,
                                 &manager->manifest);
    if (status != SYS_OK) {
        return set_failed(manager, status);
    }
    status = validate_manifest_metadata(manager, &metadata);
    if (status != SYS_OK) {
        return status;
    }

    enter_critical(manager);
    manager->status.active_slot = metadata.active_slot;
    manager->status.target_slot = (app_slot_t)manager->manifest.target_slot;
    memcpy(manager->status.manifest_version,
           manager->manifest.version,
           sizeof(manager->status.manifest_version));
    memcpy(manager->status.download_url,
           manager->manifest.download_url,
           sizeof(manager->status.download_url));
    manager->status.manifest_valid = 1u;
    reset_transfer_status(manager);
    if (manager->abort_requested != 0u) {
        manager->status.state = OTA_FAILED;
        manager->status.last_error = ERR_OTA_ABORTED;
        exit_critical(manager);
        return ERR_OTA_ABORTED;
    }
    manager->status.state = OTA_READY_TO_DOWNLOAD;
    manager->status.last_error = SYS_OK;
    exit_critical(manager);
    return SYS_OK;
}

static status_t download_image_stream(ota_manager_t *manager)
{
    uint32_t downloaded = 0u;
    status_t status;

    while (downloaded < manager->manifest.image_size) {
        uint32_t remaining = manager->manifest.image_size - downloaded;
        size_t capacity = remaining < sizeof(manager->download_buffer)
                              ? (size_t)remaining
                              : sizeof(manager->download_buffer);
        size_t length = 0u;

        if (abort_requested(manager)) {
            (void)close_connection(manager);
            return set_failed(manager, ERR_OTA_ABORTED);
        }
        status = manager->port.http_read(
            manager->port.context, manager->download_buffer, capacity, &length);
        if (abort_requested(manager)) {
            status = ERR_OTA_ABORTED;
        }
        if (status != SYS_OK || length == 0u || length > capacity) {
            (void)close_connection(manager);
            return set_failed(manager, status != SYS_OK ? status : ERR_HTTP);
        }
        status = manager->port.staging_write(manager->port.context,
                                             downloaded,
                                             manager->download_buffer,
                                             length);
        if (status == SYS_OK) {
            status =
                crc32_update(&manager->crc32, manager->download_buffer, length);
        }
        if (status == SYS_OK) {
            status = sha256_update(
                &manager->sha256, manager->download_buffer, length);
        }
        if (status != SYS_OK) {
            (void)close_connection(manager);
            return set_failed(manager, status);
        }
        downloaded += (uint32_t)length;
        enter_critical(manager);
        manager->status.bytes_downloaded = downloaded;
        manager->status.chunk_count++;
        exit_critical(manager);
    }
    return SYS_OK;
}

static status_t verify_downloaded_image(ota_manager_t *manager)
{
    uint32_t actual_crc32;
    uint8_t actual_sha256[IMAGE_SHA256_LEN];
    status_t status;

    enter_critical(manager);
    manager->status.state = OTA_VERIFYING;
    exit_critical(manager);
    status = crc32_final(&manager->crc32, &actual_crc32);
    if (status == SYS_OK) {
        status = sha256_final(&manager->sha256, actual_sha256);
    }
    if (status != SYS_OK) {
        return set_failed(manager, status);
    }
    enter_critical(manager);
    manager->status.actual_crc32 = actual_crc32;
    memcpy(manager->status.actual_sha256, actual_sha256, sizeof(actual_sha256));
    exit_critical(manager);
    if (actual_crc32 != manager->manifest.crc32) {
        return set_failed(manager, ERR_CRC);
    }
    enter_critical(manager);
    manager->status.crc_verified = 1u;
    exit_critical(manager);
    if (memcmp(actual_sha256,
               manager->manifest.sha256,
               sizeof(actual_sha256)) != 0) {
        return set_failed(manager, ERR_SHA256);
    }
    enter_critical(manager);
    if (manager->abort_requested != 0u) {
        manager->status.state = OTA_FAILED;
        manager->status.last_error = ERR_OTA_ABORTED;
        exit_critical(manager);
        return ERR_OTA_ABORTED;
    }
    manager->status.sha256_verified = 1u;
    manager->status.state = OTA_READY;
    manager->status.last_error = SYS_OK;
    exit_critical(manager);
    return SYS_OK;
}

status_t ota_manager_download(ota_manager_t *manager)
{
    uint32_t content_length = 0u;
    status_t status;

    if (manager == 0 || manager->initialized == 0u) {
        return ERR_INVALID_ARG;
    }
    enter_critical(manager);
    if (manager->status.state != OTA_READY_TO_DOWNLOAD ||
        manager->status.manifest_valid == 0u) {
        exit_critical(manager);
        return ERR_INVALID_ARG;
    }
    manager->abort_requested = 0u;
    reset_transfer_status(manager);
    manager->status.state = OTA_DOWNLOADING;
    manager->status.last_error = SYS_OK;
    exit_critical(manager);

    status = crc32_init(&manager->crc32);
    if (status == SYS_OK) {
        status = sha256_init(&manager->sha256);
    }
    if (status == SYS_OK) {
        status = manager->port.staging_begin(manager->port.context,
                                             manager->manifest.image_size);
    }
    if (status != SYS_OK) {
        return set_failed(manager, status);
    }
    manager->connection_open = 1u;
    status = manager->port.http_open(
        manager->port.context, manager->manifest.download_url, &content_length);
    if (abort_requested(manager)) {
        status = ERR_OTA_ABORTED;
    }
    if (status != SYS_OK || content_length != manager->manifest.image_size) {
        (void)close_connection(manager);
        return set_failed(manager, status != SYS_OK ? status : ERR_HTTP);
    }

    status = download_image_stream(manager);
    if (status != SYS_OK) {
        return status;
    }
    status = close_connection(manager);
    if (status != SYS_OK) {
        return set_failed(manager, status);
    }
    return verify_downloaded_image(manager);
}

status_t ota_manager_abort(ota_manager_t *manager)
{
    ota_state_t state;

    if (manager == 0 || manager->initialized == 0u) {
        return ERR_INVALID_ARG;
    }
    enter_critical(manager);
    state = manager->status.state;
    if (manager->commit_in_progress != 0u ||
        (state != OTA_CHECKING && state != OTA_READY_TO_DOWNLOAD &&
         state != OTA_DOWNLOADING && state != OTA_VERIFYING &&
         state != OTA_READY)) {
        exit_critical(manager);
        return ERR_INVALID_ARG;
    }
    manager->abort_requested = 1u;
    if (state == OTA_READY_TO_DOWNLOAD || state == OTA_READY) {
        manager->status.state = OTA_FAILED;
        manager->status.last_error = ERR_OTA_ABORTED;
    }
    exit_critical(manager);
    return SYS_OK;
}

status_t ota_manager_get_status(const ota_manager_t *manager,
                                ota_status_t *out_status)
{
    if (manager == 0 || manager->initialized == 0u || out_status == 0) {
        return ERR_INVALID_ARG;
    }
    enter_critical((ota_manager_t *)manager);
    *out_status = manager->status;
    exit_critical((ota_manager_t *)manager);
    return SYS_OK;
}

static status_t build_metadata_record(ota_manager_t *manager,
                                      const ota_status_t *snapshot,
                                      ota_metadata_record_t *record,
                                      uint8_t *encoded)
{
    status_t status;

    memset(record, 0, sizeof(*record));
    record->magic = OTA_STAGING_METADATA_MAGIC;
    record->version = OTA_STAGING_METADATA_VERSION;
    record->record_size = OTA_STAGING_METADATA_WIRE_SIZE;
    record->target_slot = (uint32_t)snapshot->target_slot;
    record->image_size = manager->manifest.image_size;
    record->downloaded_size = snapshot->bytes_downloaded;
    record->image_crc32 = snapshot->actual_crc32;
    memcpy(record->image_sha256, snapshot->actual_sha256, IMAGE_SHA256_LEN);
    memcpy(record->image_version,
           manager->manifest.version,
           sizeof(record->image_version));
    memcpy(record->download_url,
           manager->manifest.download_url,
           sizeof(record->download_url));
    status =
        ota_metadata_encode(record, encoded, OTA_STAGING_METADATA_WIRE_SIZE);
    if (status == SYS_OK) {
        status = crc32_compute(
            encoded, OTA_STAGING_METADATA_WIRE_SIZE, &record->record_crc32);
    }
    if (status == SYS_OK) {
        status = ota_metadata_encode(
            record, encoded, OTA_STAGING_METADATA_WIRE_SIZE);
    }
    return status;
}

status_t ota_manager_commit_pending(ota_manager_t *manager)
{
    boot_metadata_t current;
    boot_metadata_t desired;
    boot_metadata_t committed;
    app_slot_t current_copy = SLOT_NONE;
    app_slot_t committed_copy = SLOT_NONE;
    ota_status_t snapshot;
    ota_metadata_record_t record;
    uint8_t encoded[OTA_STAGING_METADATA_WIRE_SIZE];
    status_t status;

    if (manager == 0 || manager->initialized == 0u) {
        return ERR_INVALID_ARG;
    }
    enter_critical(manager);
    snapshot = manager->status;
    if (snapshot.state != OTA_READY || snapshot.manifest_valid == 0u ||
        snapshot.crc_verified == 0u || snapshot.sha256_verified == 0u ||
        manager->commit_in_progress != 0u) {
        exit_critical(manager);
        return ERR_INVALID_ARG;
    }
    manager->commit_in_progress = 1u;
    exit_critical(manager);

    status = manager->port.metadata_load(
        manager->port.context, &current, &current_copy);
    if (status == SYS_OK) {
        status = validate_normal_metadata(&current);
    }
    if (status == SYS_OK && (snapshot.target_slot == current.active_slot ||
                             snapshot.active_slot != current.active_slot)) {
        status = ERR_SLOT_MISMATCH;
    }
    if (status == SYS_OK) {
        status = build_metadata_record(manager, &snapshot, &record, encoded);
    }
    if (status == SYS_OK) {
        status = manager->port.ota_metadata_commit(
            manager->port.context, encoded, sizeof(encoded));
    }
    if (status == SYS_OK) {
        status = boot_meta_request_staging(
            &current,
            &(const boot_staging_request_t){snapshot.target_slot,
                                            manager->manifest.version,
                                            snapshot.actual_crc32,
                                            snapshot.actual_sha256,
                                            &desired});
    }
    if (status == SYS_OK) {
        status = manager->port.metadata_commit(
            manager->port.context,
            &(const boot_meta_commit_request_t){
                &current, current_copy, &desired, &committed, &committed_copy});
    }
    if (status == SYS_OK) {
        status = boot_meta_validate(&committed);
    }
    if (status == SYS_OK && (committed.boot_state != BOOT_STATE_PENDING ||
                             committed.active_slot != current.active_slot ||
                             committed.pending_slot != snapshot.target_slot ||
                             committed.sequence <= current.sequence)) {
        status = ERR_METADATA_INVALID;
    }
    if (status != SYS_OK) {
        return set_failed(manager, status);
    }
    (void)committed_copy;
    enter_critical(manager);
    manager->commit_in_progress = 0u;
    manager->status.state = OTA_REBOOT_PENDING;
    manager->status.last_error = SYS_OK;
    exit_critical(manager);
    return SYS_OK;
}

const char *ota_state_name(ota_state_t state)
{
    switch (state) {
    case OTA_IDLE:
        return "idle";
    case OTA_CHECKING:
        return "checking";
    case OTA_READY_TO_DOWNLOAD:
        return "ready-download";
    case OTA_DOWNLOADING:
        return "downloading";
    case OTA_VERIFYING:
        return "verifying";
    case OTA_READY:
        return "ready";
    case OTA_REBOOT_PENDING:
        return "reboot-pending";
    case OTA_FAILED:
        return "failed";
    default:
        return "unknown";
    }
}
