#include "image_verify.h"

#include "crc32.h"
#include "platform_constants.h"
#include "sha256.h"

#include <string.h>

status_t image_verify_header(const image_header_t *header)
{
    return image_header_validate_basic(header);
}

status_t image_verify_slot_binding(const image_header_t *header,
                                   app_slot_t expected_slot)
{
    return image_header_validate_for_slot(header, expected_slot);
}

static status_t validate_stream_args(const image_header_t *header,
                                     uint32_t reader_offset,
                                     image_verify_read_fn read_fn,
                                     const uint8_t *scratch,
                                     size_t scratch_size)
{
    status_t status;

    if (header == 0 || read_fn == 0 || scratch == 0 || scratch_size == 0u) {
        return ERR_INVALID_ARG;
    }

    status = image_verify_header(header);
    if (status != SYS_OK) {
        return status;
    }
    if (reader_offset > UINT32_MAX - header->image_size) {
        return ERR_IMAGE_INVALID;
    }

    return SYS_OK;
}

status_t image_verify_crc32(const image_header_t *header,
                            image_verify_read_fn read_fn,
                            void *context,
                            uint8_t *scratch,
                            size_t scratch_size)
{
    if (header == 0) {
        return ERR_INVALID_ARG;
    }
    return image_verify_crc32_at(
        header,
        &(const image_body_check_t){
            header->image_offset, read_fn, context, scratch, scratch_size});
}

status_t image_verify_crc32_at(const image_header_t *header,
                               const image_body_check_t *parameters)
{
    if (parameters == 0) {
        return ERR_INVALID_ARG;
    }
    uint32_t reader_offset = parameters->reader_offset;
    image_verify_read_fn read_fn = parameters->read_fn;
    void *context = parameters->context;
    uint8_t *scratch = parameters->scratch;
    size_t scratch_size = parameters->scratch_size;

    crc32_context_t crc_ctx;
    uint32_t computed_crc = 0u;
    uint32_t offset;
    uint32_t remaining;
    status_t status;

    status = validate_stream_args(
        header, reader_offset, read_fn, scratch, scratch_size);
    if (status != SYS_OK) {
        return status;
    }

    status = crc32_init(&crc_ctx);
    if (status != SYS_OK) {
        return status;
    }

    offset = reader_offset;
    remaining = header->image_size;
    while (remaining != 0u) {
        size_t chunk =
            scratch_size < (size_t)remaining ? scratch_size : (size_t)remaining;

        status = read_fn(context, offset, scratch, chunk);
        if (status != SYS_OK) {
            return status;
        }
        status = crc32_update(&crc_ctx, scratch, chunk);
        if (status != SYS_OK) {
            return status;
        }

        offset += (uint32_t)chunk;
        remaining -= (uint32_t)chunk;
    }

    status = crc32_final(&crc_ctx, &computed_crc);
    if (status != SYS_OK) {
        return status;
    }

    return computed_crc == header->image_crc32 ? SYS_OK : ERR_CRC;
}

status_t image_verify_sha256(const image_header_t *header,
                             image_verify_read_fn read_fn,
                             void *context,
                             uint8_t *scratch,
                             size_t scratch_size)
{
    if (header == 0) {
        return ERR_INVALID_ARG;
    }
    return image_verify_sha256_at(
        header,
        &(const image_body_check_t){
            header->image_offset, read_fn, context, scratch, scratch_size});
}

status_t image_verify_sha256_at(const image_header_t *header,
                                const image_body_check_t *parameters)
{
    if (parameters == 0) {
        return ERR_INVALID_ARG;
    }
    uint32_t reader_offset = parameters->reader_offset;
    image_verify_read_fn read_fn = parameters->read_fn;
    void *context = parameters->context;
    uint8_t *scratch = parameters->scratch;
    size_t scratch_size = parameters->scratch_size;

    sha256_context_t sha_ctx;
    uint8_t computed_digest[SHA256_DIGEST_SIZE];
    uint32_t offset;
    uint32_t remaining;
    status_t status;

    status = validate_stream_args(
        header, reader_offset, read_fn, scratch, scratch_size);
    if (status != SYS_OK) {
        return status;
    }

    status = sha256_init(&sha_ctx);
    if (status != SYS_OK) {
        return status;
    }

    offset = reader_offset;
    remaining = header->image_size;
    while (remaining != 0u) {
        size_t chunk =
            scratch_size < (size_t)remaining ? scratch_size : (size_t)remaining;

        status = read_fn(context, offset, scratch, chunk);
        if (status != SYS_OK) {
            return status;
        }
        status = sha256_update(&sha_ctx, scratch, chunk);
        if (status != SYS_OK) {
            return status;
        }

        offset += (uint32_t)chunk;
        remaining -= (uint32_t)chunk;
    }

    status = sha256_final(&sha_ctx, computed_digest);
    if (status != SYS_OK) {
        return status;
    }

    return memcmp(computed_digest, header->image_sha256, SHA256_DIGEST_SIZE) ==
                   0
               ? SYS_OK
               : ERR_SHA256;
}

static uint32_t read_u32_le(const uint8_t bytes[4])
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

status_t image_verify_vector_table_at(const image_header_t *header,
                                      app_slot_t expected_slot,
                                      uint32_t reader_offset,
                                      image_verify_read_fn read_fn,
                                      void *context)
{
    const partition_t *image_partition;
    uint8_t vectors[8];
    uint32_t initial_msp;
    uint32_t reset_handler;
    uint32_t reset_address;
    uint32_t image_end;
    status_t status;

    if (header == 0 || read_fn == 0) {
        return ERR_INVALID_ARG;
    }

    status = image_header_validate_for_slot(header, expected_slot);
    if (status != SYS_OK) {
        return status;
    }
    if (header->image_size < sizeof(vectors) ||
        reader_offset > UINT32_MAX - (uint32_t)sizeof(vectors)) {
        return ERR_IMAGE_INVALID;
    }

    status = read_fn(context, reader_offset, vectors, sizeof(vectors));
    if (status != SYS_OK) {
        return status;
    }

    initial_msp = read_u32_le(&vectors[0]);
    reset_handler = read_u32_le(&vectors[4]);
    if (initial_msp < PROJECT_SRAM_START || initial_msp > PROJECT_SRAM_END ||
        (initial_msp & 7u) != 0u || (reset_handler & 1u) == 0u) {
        return ERR_IMAGE_INVALID;
    }

    image_partition = partition_get_slot_image(expected_slot);
    if (image_partition == 0 ||
        header->image_size > UINT32_MAX - image_partition->start) {
        return ERR_IMAGE_INVALID;
    }
    image_end = image_partition->start + header->image_size;
    reset_address = reset_handler & ~1u;
    if (reset_address < image_partition->start || reset_address >= image_end) {
        return ERR_IMAGE_INVALID;
    }

    return SYS_OK;
}
