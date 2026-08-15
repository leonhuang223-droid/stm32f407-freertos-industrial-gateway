#ifndef IMAGE_HEADER_H
#define IMAGE_HEADER_H

#include "error_code.h"
#include "partition_table.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define IMAGE_SHA256_LEN 32
#define IMAGE_MAGIC 0x494F5441u
#define IMAGE_FLAG_CANDIDATE 0x00000001u
#define IMAGE_FLAG_CONFIRMED 0x00000002u

/**
 * @brief OTA package and installed descriptor header.
 *
 * On the download path this structure is the first bytes of the package:
 * `image_header_t + raw_app.bin`. During installation the raw body is written
 * at the slot base and this header is committed last to the slot descriptor
 * page. Header CRC is computed with `header_crc32` treated as zero.
 */
typedef struct {
    uint32_t magic;                /**< Fixed IMAGE_MAGIC marker. */
    uint16_t header_version;       /**< Header layout version. */
    uint16_t header_size;          /**< Must equal sizeof(image_header_t). */
    char target_id[32];            /**< NUL-terminated board target id. */
    char app_version[16];          /**< NUL-terminated semantic-ish version. */
    char git_sha[16];              /**< Short build revision string. */
    uint32_t target_slot;          /**< app_slot_t value encoded for wire format. */
    uint32_t link_address;         /**< Expected vector table/link address. */
    uint32_t image_offset;         /**< Body offset within the package. */
    uint32_t image_size; /* Raw application body bytes. */
    uint32_t image_crc32;          /**< CRC32 of the raw application body. */
    uint8_t image_sha256[IMAGE_SHA256_LEN]; /**< SHA256 of the raw body. */
    char min_bootloader_version[16]; /**< Minimum accepted Bootloader version. */
    uint32_t image_flags;          /**< IMAGE_FLAG_CANDIDATE/CONFIRMED. */
    uint32_t header_crc32;         /**< CRC32 over the header with this field zero. */
} image_header_t;

#define IMAGE_PACKAGE_V1_MIN_SIZE ((uint32_t)sizeof(image_header_t) + 8u)
#define IMAGE_PACKAGE_V1_MAX_SIZE \
    ((uint32_t)sizeof(image_header_t) + PARTITION_APP_IMAGE_SIZE)

typedef enum {
    IMAGE_STATE_INVALID = 0,
    IMAGE_STATE_CANDIDATE,
    IMAGE_STATE_CONFIRMED
} image_state_t;

/**
 * @brief Parse a byte buffer into an image header without validating body data.
 */
status_t image_header_parse(const uint8_t *buffer, size_t length, image_header_t *out_header);

/**
 * @brief Recompute and store the header CRC32 field.
 */
status_t image_header_refresh_crc(image_header_t *header);

/**
 * @brief Validate magic, version, size, strings, flags and header CRC.
 */
status_t image_header_validate_basic(const image_header_t *header);

/**
 * @brief Validate that a header targets the expected physical slot.
 */
status_t image_header_validate_for_slot(const image_header_t *header, app_slot_t expected_slot);

/**
 * @brief Validate package offset and total package size against body size.
 */
status_t image_header_validate_package_layout(const image_header_t *header, size_t package_size);

/**
 * @brief Convert image_flags to a descriptor state.
 */
image_state_t image_header_get_state(const image_header_t *header);

#ifdef __cplusplus
}
#endif

#endif
