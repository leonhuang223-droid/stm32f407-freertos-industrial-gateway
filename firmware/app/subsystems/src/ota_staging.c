#include "ota_staging.h"

#include "external_flash_layout.h"
#include "ota_staging_format.h"

#include <string.h>

static status_t record_result(ota_staging_t *staging, status_t status)
{
    if (staging != 0) {
        staging->health.last_error = status;
    }
    return status;
}

status_t ota_staging_construct(ota_staging_t *staging, storage_media_t *media)
{
    if (staging == 0 || media == 0 || media->sector_size == 0u ||
        media->page_size == 0u) {
        return ERR_INVALID_ARG;
    }
    memset(staging, 0, sizeof(*staging));
    staging->media = media;
    staging->health.last_error = SYS_OK;
    staging->initialized = 1u;
    return SYS_OK;
}

status_t ota_staging_begin(ota_staging_t *staging, size_t package_size)
{
    if (staging == 0 || staging->initialized == 0u || package_size == 0u ||
        package_size > EXTERNAL_FLASH_OTA_STAGING_SIZE) {
        return record_result(staging, ERR_INVALID_ARG);
    }
    staging->package_size = (uint32_t)package_size;
    staging->next_erase_address = EXTERNAL_FLASH_OTA_STAGING_START;
    staging->next_write_offset = 0u;
    staging->health.prepared_bytes = (uint32_t)package_size;
    staging->health.erased_bytes = 0u;
    staging->health.written_bytes = 0u;
    staging->health.prepared = 1u;
    staging->health.erase_complete = 0u;
    return record_result(staging, SYS_OK);
}

status_t ota_staging_erase_next(ota_staging_t *staging, uint8_t *out_complete)
{
    uint32_t required_end;
    status_t status;

    if (out_complete != 0) {
        *out_complete = 0u;
    }
    if (staging == 0 || staging->initialized == 0u ||
        staging->health.prepared == 0u || out_complete == 0) {
        return record_result(staging, ERR_INVALID_ARG);
    }
    required_end = EXTERNAL_FLASH_OTA_STAGING_START + staging->package_size;
    if (staging->next_erase_address >= required_end) {
        staging->health.erase_complete = 1u;
        *out_complete = 1u;
        return record_result(staging, SYS_OK);
    }
    status =
        storage_media_erase_sector(staging->media, staging->next_erase_address);
    if (status != SYS_OK) {
        return record_result(staging, status);
    }
    staging->next_erase_address += staging->media->sector_size;
    staging->health.erased_bytes += staging->media->sector_size;
    staging->health.erase_operations++;
    if (staging->next_erase_address >= required_end) {
        staging->health.erase_complete = 1u;
        *out_complete = 1u;
    }
    return record_result(staging, SYS_OK);
}

status_t ota_staging_write(ota_staging_t *staging,
                           uint32_t offset,
                           const uint8_t *data,
                           size_t length)
{
    uint8_t verify[EXTERNAL_FLASH_PAGE_SIZE];
    uint32_t address;
    status_t status;

    if (staging == 0 || staging->initialized == 0u || data == 0 ||
        length == 0u || length > sizeof(verify) ||
        staging->health.erase_complete == 0u ||
        offset != staging->next_write_offset ||
        offset > staging->package_size ||
        length > (size_t)(staging->package_size - offset)) {
        return record_result(staging, ERR_INVALID_ARG);
    }
    address = EXTERNAL_FLASH_OTA_STAGING_START + offset;
    status = storage_media_program(staging->media, address, data, length);
    if (status == SYS_OK) {
        status = storage_media_read(staging->media, address, verify, length);
    }
    if (status == SYS_OK && memcmp(verify, data, length) != 0) {
        status = ERR_FLASH_VERIFY;
    }
    if (status != SYS_OK) {
        return record_result(staging, status);
    }
    staging->next_write_offset += (uint32_t)length;
    staging->health.written_bytes = staging->next_write_offset;
    staging->health.write_operations++;
    return record_result(staging, SYS_OK);
}

status_t ota_staging_commit_metadata(ota_staging_t *staging,
                                     const uint8_t *record,
                                     size_t record_size)
{
    uint8_t verify[OTA_STAGING_METADATA_WIRE_SIZE];
    status_t status;

    if (staging == 0 || staging->initialized == 0u || record == 0 ||
        record_size != sizeof(verify) ||
        staging->next_write_offset != staging->package_size) {
        return record_result(staging, ERR_INVALID_ARG);
    }
    status = storage_media_erase_sector(staging->media,
                                        EXTERNAL_FLASH_OTA_METADATA_START);
    if (status == SYS_OK) {
        status = storage_media_program(staging->media,
                                       EXTERNAL_FLASH_OTA_METADATA_START,
                                       record,
                                       record_size);
    }
    if (status == SYS_OK) {
        status = storage_media_read(staging->media,
                                    EXTERNAL_FLASH_OTA_METADATA_START,
                                    verify,
                                    sizeof(verify));
    }
    if (status == SYS_OK && memcmp(verify, record, sizeof(verify)) != 0) {
        status = ERR_FLASH_VERIFY;
    }
    if (status == SYS_OK) {
        staging->health.metadata_commits++;
    }
    return record_result(staging, status);
}

status_t ota_staging_get_health(const ota_staging_t *staging,
                                ota_staging_health_t *out_health)
{
    if (staging == 0 || staging->initialized == 0u || out_health == 0) {
        return ERR_INVALID_ARG;
    }
    *out_health = staging->health;
    return SYS_OK;
}
