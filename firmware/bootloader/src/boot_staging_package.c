#include "boot_staging_package.h"

#include "crc32.h"
#include "sha256.h"
#include "version.h"

#include <string.h>

typedef struct {
    boot_staging_read_fn read_fn;
    void *read_context;
    uint32_t package_size;
} bounded_reader_t;

static uint16_t read_u16_le(const uint8_t *bytes)
{
    return (uint16_t)((uint16_t)bytes[0] |
                      (uint16_t)((uint16_t)bytes[1] << 8u));
}

static uint32_t read_u32_le(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8u) |
           ((uint32_t)bytes[2] << 16u) | ((uint32_t)bytes[3] << 24u);
}

static status_t bounded_package_read(void *context,
                                     uint32_t offset,
                                     uint8_t *buffer,
                                     size_t length)
{
    bounded_reader_t *reader = (bounded_reader_t *)context;

    if (reader == 0 || reader->read_fn == 0 || buffer == 0 || length == 0u ||
        offset > reader->package_size ||
        length > (size_t)(reader->package_size - offset)) {
        return ERR_INVALID_ARG;
    }
    return reader->read_fn(
        reader->read_context, BOOT_STAGING_START + offset, buffer, length);
}

status_t boot_staging_package_load(boot_staging_read_fn read_fn,
                                   void *read_context,
                                   const boot_metadata_t *metadata,
                                   boot_staging_package_t *out_package)
{
    uint8_t wire[BOOT_STAGING_METADATA_WIRE_SIZE];
    uint32_t expected_crc;
    uint32_t actual_crc;
    uint32_t image_size;
    uint32_t downloaded_size;
    uint32_t target_slot;
    status_t status;

    if (read_fn == 0 || metadata == 0 || out_package == 0) {
        return ERR_INVALID_ARG;
    }
    status = boot_meta_validate(metadata);
    if (status != SYS_OK || metadata->boot_state != BOOT_STATE_PENDING) {
        return status != SYS_OK ? status : ERR_METADATA_INVALID;
    }

    status =
        read_fn(read_context, BOOT_STAGING_METADATA_START, wire, sizeof(wire));
    if (status != SYS_OK) {
        return status;
    }
    if (read_u32_le(&wire[0]) != BOOT_STAGING_METADATA_MAGIC ||
        read_u16_le(&wire[4]) != BOOT_STAGING_METADATA_VERSION ||
        read_u16_le(&wire[6]) != BOOT_STAGING_METADATA_WIRE_SIZE) {
        return ERR_METADATA_INVALID;
    }

    expected_crc = read_u32_le(&wire[BOOT_STAGING_METADATA_CRC_OFFSET]);
    memset(&wire[BOOT_STAGING_METADATA_CRC_OFFSET], 0, sizeof(uint32_t));
    status = crc32_compute(wire, sizeof(wire), &actual_crc);
    if (status != SYS_OK) {
        return status;
    }
    if (actual_crc != expected_crc) {
        return ERR_CRC;
    }

    target_slot = read_u32_le(&wire[8]);
    image_size = read_u32_le(&wire[12]);
    downloaded_size = read_u32_le(&wire[16]);
    if (target_slot != (uint32_t)metadata->pending_slot) {
        return ERR_SLOT_MISMATCH;
    }
    if (image_size != downloaded_size ||
        image_size < IMAGE_PACKAGE_V1_MIN_SIZE ||
        image_size > IMAGE_PACKAGE_V1_MAX_SIZE ||
        image_size > BOOT_STAGING_SIZE) {
        return ERR_IMAGE_INVALID;
    }
    if (read_u32_le(&wire[20]) != metadata->staging_image_crc32) {
        return ERR_CRC;
    }
    if (memcmp(&wire[24], metadata->staging_image_sha256, IMAGE_SHA256_LEN) !=
        0) {
        return ERR_SHA256;
    }

    memset(out_package, 0, sizeof(*out_package));
    out_package->package_size = image_size;
    out_package->target_slot = metadata->pending_slot;
    out_package->package_crc32 = read_u32_le(&wire[20]);
    memcpy(out_package->package_sha256, &wire[24], IMAGE_SHA256_LEN);
    return SYS_OK;
}

static status_t verify_package_digest(bounded_reader_t *reader,
                                      const boot_staging_package_t *package,
                                      uint8_t *scratch,
                                      size_t scratch_size)
{
    crc32_context_t crc;
    sha256_context_t sha;
    uint8_t digest[IMAGE_SHA256_LEN];
    uint32_t actual_crc;
    uint32_t offset = 0u;
    status_t status;

    status = crc32_init(&crc);
    if (status != SYS_OK) {
        return status;
    }
    status = sha256_init(&sha);
    if (status != SYS_OK) {
        return status;
    }

    while (offset < package->package_size) {
        uint32_t remaining = package->package_size - offset;
        size_t chunk =
            scratch_size < (size_t)remaining ? scratch_size : (size_t)remaining;

        status = bounded_package_read(reader, offset, scratch, chunk);
        if (status != SYS_OK) {
            return status;
        }
        status = crc32_update(&crc, scratch, chunk);
        if (status != SYS_OK) {
            return status;
        }
        status = sha256_update(&sha, scratch, chunk);
        if (status != SYS_OK) {
            return status;
        }
        offset += (uint32_t)chunk;
    }

    status = crc32_final(&crc, &actual_crc);
    if (status != SYS_OK) {
        return status;
    }
    if (actual_crc != package->package_crc32) {
        return ERR_CRC;
    }
    status = sha256_final(&sha, digest);
    if (status != SYS_OK) {
        return status;
    }
    return memcmp(digest, package->package_sha256, sizeof(digest)) == 0
               ? SYS_OK
               : ERR_SHA256;
}

status_t
boot_staging_validate_min_bootloader_version(const image_header_t *header)
{
    int compare_result;
    status_t status;

    if (header == 0 || header->min_bootloader_version[0] == '\0' ||
        memchr(header->min_bootloader_version,
               '\0',
               sizeof(header->min_bootloader_version)) == 0) {
        return ERR_IMAGE_INVALID;
    }

    status = version_compare(
        header->min_bootloader_version, BOOTLOADER_VERSION, &compare_result);
    if (status != SYS_OK) {
        return ERR_IMAGE_INVALID;
    }
    return compare_result <= 0 ? SYS_OK : ERR_UNSUPPORTED;
}

static status_t validate_staging_header(bounded_reader_t *reader,
                                        const boot_staging_package_t *package,
                                        image_header_t *header)
{
    status_t status;

    status =
        bounded_package_read(reader, 0u, (uint8_t *)header, sizeof(*header));
    if (status != SYS_OK) {
        return status;
    }
    status = image_header_validate_for_slot(header, package->target_slot);
    if (status != SYS_OK ||
        image_header_get_state(header) != IMAGE_STATE_CANDIDATE) {
        return status != SYS_OK ? status : ERR_IMAGE_INVALID;
    }
    status = boot_staging_validate_min_bootloader_version(header);
    if (status != SYS_OK) {
        return status;
    }
    status =
        image_header_validate_package_layout(header, package->package_size);
    if (status != SYS_OK) {
        return status;
    }
    return SYS_OK;
}

status_t
boot_staging_package_validate(boot_staging_read_fn read_fn,
                              const boot_package_validation_t *parameters)
{
    if (parameters == 0) {
        return ERR_INVALID_ARG;
    }
    void *read_context = parameters->read_context;
    const boot_metadata_t *metadata = parameters->metadata;
    uint8_t *scratch = parameters->scratch;
    size_t scratch_size = parameters->scratch_size;
    boot_staging_package_t *out_package = parameters->out_package;
    image_header_t *out_header = parameters->out_header;

    boot_staging_package_t package;
    bounded_reader_t reader;
    image_header_t header;
    status_t status;

    if (scratch == 0 || scratch_size == 0u) {
        return ERR_INVALID_ARG;
    }
    status =
        boot_staging_package_load(read_fn, read_context, metadata, &package);
    if (status != SYS_OK) {
        return status;
    }

    reader.read_fn = read_fn;
    reader.read_context = read_context;
    reader.package_size = package.package_size;
    status = verify_package_digest(&reader, &package, scratch, scratch_size);
    if (status != SYS_OK) {
        return status;
    }
    status = validate_staging_header(&reader, &package, &header);
    if (status != SYS_OK) {
        return status;
    }
    status = image_verify_vector_table_at(&header,
                                          package.target_slot,
                                          header.image_offset,
                                          bounded_package_read,
                                          &reader);
    if (status != SYS_OK) {
        return status;
    }
    status = image_verify_crc32(
        &header, bounded_package_read, &reader, scratch, scratch_size);
    if (status != SYS_OK) {
        return status;
    }
    status = image_verify_sha256(
        &header, bounded_package_read, &reader, scratch, scratch_size);
    if (status != SYS_OK) {
        return status;
    }

    if (out_package != 0) {
        *out_package = package;
    }
    if (out_header != 0) {
        *out_header = header;
    }
    return SYS_OK;
}
