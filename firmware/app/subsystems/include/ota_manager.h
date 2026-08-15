#ifndef GATEWAY_OTA_MANAGER_H
#define GATEWAY_OTA_MANAGER_H

#include "boot_metadata.h"
#include "crc32.h"
#include "manifest.h"
#include "ota_staging_format.h"
#include "sha256.h"

#include <stddef.h>
#include <stdint.h>

#define OTA_MANAGER_MANIFEST_BUFFER_SIZE 768u
#define OTA_MANAGER_DOWNLOAD_CHUNK_SIZE 256u

typedef enum {
    OTA_IDLE = 0,
    OTA_CHECKING,
    OTA_READY_TO_DOWNLOAD,
    OTA_DOWNLOADING,
    OTA_VERIFYING,
    OTA_READY,
    OTA_REBOOT_PENDING,
    OTA_FAILED
} ota_state_t;

typedef struct {
    ota_state_t state;
    status_t last_error;
    uint32_t bytes_downloaded;
    uint32_t total_bytes;
    uint32_t chunk_count;
    uint32_t expected_crc32;
    uint32_t actual_crc32;
    uint8_t expected_sha256[IMAGE_SHA256_LEN];
    uint8_t actual_sha256[IMAGE_SHA256_LEN];
    app_slot_t active_slot;
    app_slot_t target_slot;
    char manifest_version[MANIFEST_VERSION_LEN];
    char download_url[MANIFEST_DOWNLOAD_URL_LEN];
    uint8_t manifest_valid;
    uint8_t crc_verified;
    uint8_t sha256_verified;
} ota_status_t;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t record_size;
    uint32_t target_slot;
    uint32_t image_size;
    uint32_t downloaded_size;
    uint32_t image_crc32;
    uint8_t image_sha256[IMAGE_SHA256_LEN];
    char image_version[MANIFEST_VERSION_LEN];
    char download_url[MANIFEST_DOWNLOAD_URL_LEN];
    uint32_t record_crc32;
} ota_metadata_record_t;

/** Port contract keeps policy independent from HTTP, RTOS, and Flash. */
typedef struct {
    status_t (*fetch_manifest)(void *context, const char *url,
                               uint8_t *buffer, size_t capacity,
                               size_t *out_length);
    status_t (*http_open)(void *context, const char *url,
                          uint32_t *out_content_length);
    status_t (*http_read)(void *context, uint8_t *buffer,
                          size_t capacity, size_t *out_length);
    status_t (*http_close)(void *context);
    status_t (*staging_begin)(void *context, size_t package_size);
    status_t (*staging_write)(void *context, uint32_t offset,
                              const uint8_t *data, size_t length);
    status_t (*metadata_load)(void *context, boot_metadata_t *out_metadata,
                              app_slot_t *out_copy_slot);
    status_t (*metadata_commit)(void *context,
                                const boot_metadata_t *current,
                                app_slot_t current_copy_slot,
                                const boot_metadata_t *desired,
                                boot_metadata_t *out_committed,
                                app_slot_t *out_copy_slot);
    status_t (*ota_metadata_commit)(void *context,
                                    const uint8_t *record,
                                    size_t record_size);
    void (*enter_critical)(void *context);
    void (*exit_critical)(void *context);
    void *context;
} ota_manager_port_t;

typedef struct {
    const char *manifest_url;
    const char *target_id;
    const char *current_version;
    const char *current_bootloader_version;
} ota_manager_config_t;

typedef struct {
    ota_manager_port_t port;
    ota_manifest_t manifest;
    ota_status_t status;
    crc32_context_t crc32;
    sha256_context_t sha256;
    char manifest_url[MANIFEST_DOWNLOAD_URL_LEN];
    char target_id[MANIFEST_TARGET_ID_LEN];
    char current_version[MANIFEST_VERSION_LEN];
    char current_bootloader_version[MANIFEST_MIN_BOOTLOADER_VERSION_LEN];
    uint8_t manifest_buffer[OTA_MANAGER_MANIFEST_BUFFER_SIZE];
    uint8_t download_buffer[OTA_MANAGER_DOWNLOAD_CHUNK_SIZE];
    uint8_t initialized;
    volatile uint8_t abort_requested;
    uint8_t connection_open;
    uint8_t commit_in_progress;
} ota_manager_t;

status_t ota_metadata_encode(const ota_metadata_record_t *record,
                             uint8_t *buffer, size_t capacity);
status_t ota_manager_construct(ota_manager_t *manager,
                               const ota_manager_port_t *port,
                               const ota_manager_config_t *config);
status_t ota_manager_check(ota_manager_t *manager);
status_t ota_manager_download(ota_manager_t *manager);
status_t ota_manager_abort(ota_manager_t *manager);
status_t ota_manager_get_status(const ota_manager_t *manager,
                                ota_status_t *out_status);
status_t ota_manager_commit_pending(ota_manager_t *manager);
const char *ota_state_name(ota_state_t state);

#endif
