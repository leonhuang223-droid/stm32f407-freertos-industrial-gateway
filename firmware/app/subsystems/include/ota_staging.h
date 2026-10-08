#ifndef GATEWAY_OTA_STAGING_H
#define GATEWAY_OTA_STAGING_H

#include "error_code.h"
#include "storage_media.h"

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t prepared_bytes;
    uint32_t erased_bytes;
    uint32_t written_bytes;
    uint32_t erase_operations;
    uint32_t write_operations;
    uint32_t metadata_commits;
    status_t last_error;
    uint8_t prepared;
    uint8_t erase_complete;
} ota_staging_health_t;

/** Sequential W25Q128 OTA staging writer owned by Storage Task. */
typedef struct {
    storage_media_t *media;
    uint32_t package_size;
    uint32_t next_erase_address;
    uint32_t next_write_offset;
    ota_staging_health_t health;
    uint8_t initialized;
} ota_staging_t;

status_t ota_staging_construct(ota_staging_t *staging, storage_media_t *media);
status_t ota_staging_begin(ota_staging_t *staging, size_t package_size);
status_t ota_staging_erase_next(ota_staging_t *staging, uint8_t *out_complete);
status_t ota_staging_write(ota_staging_t *staging,
                           uint32_t offset,
                           const uint8_t *data,
                           size_t length);
status_t ota_staging_commit_metadata(ota_staging_t *staging,
                                     const uint8_t *record,
                                     size_t record_size);
status_t ota_staging_get_health(const ota_staging_t *staging,
                                ota_staging_health_t *out_health);

#endif
