#ifndef BOOT_STAGING_PACKAGE_H
#define BOOT_STAGING_PACKAGE_H

#include "external_flash_layout.h"

#include "boot_metadata.h"
#include "image_header.h"
#include "image_verify.h"
#include "ota_staging_format.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** W25Q128 staging byte offset where image_header + raw_app.bin starts. */
#define BOOT_STAGING_START EXTERNAL_FLASH_OTA_STAGING_START
/** Maximum staged package area used by the bootloader. */
#define BOOT_STAGING_SIZE EXTERNAL_FLASH_OTA_STAGING_SIZE
/** Offset of the staging metadata record in external flash. */
#define BOOT_STAGING_METADATA_START EXTERNAL_FLASH_OTA_METADATA_START
/** Magic value for serialized staging metadata. */
#define BOOT_STAGING_METADATA_MAGIC OTA_STAGING_METADATA_MAGIC
/** Serialized staging metadata schema version. */
#define BOOT_STAGING_METADATA_VERSION OTA_STAGING_METADATA_VERSION
/** Serialized staging metadata size in bytes. */
#define BOOT_STAGING_METADATA_WIRE_SIZE OTA_STAGING_METADATA_WIRE_SIZE
/** CRC field offset inside serialized staging metadata. */
#define BOOT_STAGING_METADATA_CRC_OFFSET OTA_STAGING_METADATA_CRC_OFFSET

/** Reader for W25Q128 staging bytes. */
typedef status_t (*boot_staging_read_fn)(void *context,
                                         uint32_t address,
                                         uint8_t *buffer,
                                         size_t length);

/** Validated staging package metadata used for install. */
typedef struct {
    uint32_t package_size;  /**< Total staged package bytes. */
    app_slot_t target_slot; /**< Slot encoded by package header. */
    uint32_t package_crc32; /**< CRC32 over the package bytes. */
    uint8_t package_sha256[IMAGE_SHA256_LEN]; /**< SHA256 over package bytes. */
} boot_staging_package_t;

/**
 * @brief Load serialized staging metadata from external flash.
 * @param read_fn Staging reader callback.
 * @param read_context Reader context.
 * @param metadata Boot metadata with pending package information.
 * @param out_package Destination package metadata.
 * @return SYS_OK when metadata magic, size, and CRC are valid.
 */
status_t boot_staging_package_load(boot_staging_read_fn read_fn,
                                   void *read_context,
                                   const boot_metadata_t *metadata,
                                   boot_staging_package_t *out_package);
/**
 * @brief Check an image header against the compiled bootloader version.
 * @param header Staged package header.
 * @return SYS_OK when this bootloader is new enough to install it.
 */
status_t
boot_staging_validate_min_bootloader_version(const image_header_t *header);
/** Synchronous request; pointed-to buffers remain caller-owned.
 * @author 兆鸣嵌入式
 */
typedef struct {
    void *read_context;
    const boot_metadata_t *metadata;
    uint8_t *scratch;
    size_t scratch_size;
    boot_staging_package_t *out_package;
    image_header_t *out_header;
} boot_package_validation_t;

/**
 * @brief Validate staged package metadata, header, CRC, SHA, and vector table.
 * @param read_fn Staging reader callback.
 * @param parameters Reader context, pending metadata, scratch and outputs.
 * All pointed-to data remains valid until this synchronous call returns.
 * @return SYS_OK when the staged package can be installed.
 */
status_t
boot_staging_package_validate(boot_staging_read_fn read_fn,
                              const boot_package_validation_t *parameters);

#ifdef __cplusplus
}
#endif

#endif
