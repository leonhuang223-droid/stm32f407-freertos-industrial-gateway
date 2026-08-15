#ifndef MANIFEST_H
#define MANIFEST_H

#include "error_code.h"
#include "image_header.h"
#include "partition_table.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Maximum bytes, including NUL, for the board target identifier. */
#define MANIFEST_TARGET_ID_LEN 32
/** Maximum bytes, including NUL, for the semantic firmware version. */
#define MANIFEST_VERSION_LEN 16
/** Maximum bytes, including NUL, for the firmware package URL. */
#define MANIFEST_DOWNLOAD_URL_LEN 160
/** Maximum bytes, including NUL, for the required bootloader version. */
#define MANIFEST_MIN_BOOTLOADER_VERSION_LEN 16
/** Maximum bytes, including NUL, for a short release note. */
#define MANIFEST_RELEASE_NOTE_LEN 128

/** OTA manifest parsed from JSON before HTTP package download. */
typedef struct {
    uint16_t manifest_version; /**< Manifest schema version. */
    char target_id[MANIFEST_TARGET_ID_LEN]; /**< Expected board target ID. */
    char version[MANIFEST_VERSION_LEN]; /**< Firmware version string. */
    uint32_t target_slot; /**< Intended application slot as app_slot_t. */
    uint32_t link_address; /**< Vector table address expected in the image. */
    uint32_t image_size; /* Total package bytes: header + raw application body. */
    uint32_t crc32; /**< CRC32 over the complete header + body package. */
    uint8_t sha256[IMAGE_SHA256_LEN]; /**< SHA256 over the complete package. */
    char download_url[MANIFEST_DOWNLOAD_URL_LEN]; /**< HTTP URL for package bytes. */
    char min_bootloader_version[MANIFEST_MIN_BOOTLOADER_VERSION_LEN]; /**< Required bootloader version. */
    uint32_t force_update; /**< Non-zero allows equal/downgrade version policy. */
    char release_note[MANIFEST_RELEASE_NOTE_LEN]; /**< Human-readable release summary. */
} ota_manifest_t;

/** Local firmware identity used to validate an OTA manifest. */
typedef struct {
    const char *target_id; /**< Current board target ID. */
    const char *current_version; /**< Currently running app version. */
    const char *current_bootloader_version; /**< Installed bootloader version. */
} manifest_validate_context_t;

/**
 * @brief Parse OTA manifest JSON into a fixed-size C structure.
 * @param json NUL-terminated JSON document.
 * @param out_manifest Destination manifest.
 * @return SYS_OK on success or an error when required fields are invalid.
 */
status_t manifest_parse_json(const char *json, ota_manifest_t *out_manifest);
/**
 * @brief Validate manifest target, slot, version, and bootloader requirement.
 * @param manifest Parsed manifest.
 * @param context Current device identity.
 * @return SYS_OK when the manifest is applicable to this device.
 */
status_t manifest_validate(const ota_manifest_t *manifest, const manifest_validate_context_t *context);

#ifdef __cplusplus
}
#endif

#endif
