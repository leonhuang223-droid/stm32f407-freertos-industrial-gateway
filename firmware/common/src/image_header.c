#include "image_header.h"

#include "crc32.h"
#include "platform_constants.h"

#include <string.h>

static void image_header_crc32_update_byte(crc32_context_t *ctx, uint8_t value)
{
    (void)crc32_update(ctx, &value, 1u);
}

static void image_header_crc32_update_u16_le(crc32_context_t *ctx, uint16_t value)
{
    image_header_crc32_update_byte(ctx, (uint8_t)(value & 0xFFu));
    image_header_crc32_update_byte(ctx, (uint8_t)((value >> 8) & 0xFFu));
}

static void image_header_crc32_update_u32_le(crc32_context_t *ctx, uint32_t value)
{
    image_header_crc32_update_byte(ctx, (uint8_t)(value & 0xFFu));
    image_header_crc32_update_byte(ctx, (uint8_t)((value >> 8) & 0xFFu));
    image_header_crc32_update_byte(ctx, (uint8_t)((value >> 16) & 0xFFu));
    image_header_crc32_update_byte(ctx, (uint8_t)((value >> 24) & 0xFFu));
}

static uint32_t image_header_crc32(const image_header_t *header)
{
    crc32_context_t ctx;
    uint32_t crc = 0u;
    size_t i;

    (void)crc32_init(&ctx);

    image_header_crc32_update_u32_le(&ctx, header->magic);
    image_header_crc32_update_u16_le(&ctx, header->header_version);
    image_header_crc32_update_u16_le(&ctx, header->header_size);
    for (i = 0u; i < sizeof(header->target_id); ++i) {
        image_header_crc32_update_byte(&ctx, (uint8_t)header->target_id[i]);
    }
    for (i = 0u; i < sizeof(header->app_version); ++i) {
        image_header_crc32_update_byte(&ctx, (uint8_t)header->app_version[i]);
    }
    for (i = 0u; i < sizeof(header->git_sha); ++i) {
        image_header_crc32_update_byte(&ctx, (uint8_t)header->git_sha[i]);
    }
    image_header_crc32_update_u32_le(&ctx, header->target_slot);
    image_header_crc32_update_u32_le(&ctx, header->link_address);
    image_header_crc32_update_u32_le(&ctx, header->image_offset);
    image_header_crc32_update_u32_le(&ctx, header->image_size);
    image_header_crc32_update_u32_le(&ctx, header->image_crc32);
    for (i = 0u; i < sizeof(header->image_sha256); ++i) {
        image_header_crc32_update_byte(&ctx, header->image_sha256[i]);
    }
    for (i = 0u; i < sizeof(header->min_bootloader_version); ++i) {
        image_header_crc32_update_byte(&ctx, (uint8_t)header->min_bootloader_version[i]);
    }
    image_header_crc32_update_u32_le(&ctx, header->image_flags);
    image_header_crc32_update_u32_le(&ctx, 0u);

    (void)crc32_final(&ctx, &crc);
    return crc;
}

static int fixed_string_nonempty_and_terminated(const char *text, size_t capacity)
{
    size_t i;

    if (text[0] == '\0') {
        return 0;
    }
    for (i = 0u; i < capacity; ++i) {
        if (text[i] == '\0') {
            return 1;
        }
    }
    return 0;
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

status_t image_header_parse(const uint8_t *buffer, size_t length, image_header_t *out_header)
{
    if (buffer == 0 || out_header == 0 || length < sizeof(image_header_t)) {
        return ERR_INVALID_ARG;
    }

    memcpy(out_header, buffer, sizeof(image_header_t));
    return SYS_OK;
}

status_t image_header_refresh_crc(image_header_t *header)
{
    if (header == 0) {
        return ERR_INVALID_ARG;
    }

    header->header_crc32 = image_header_crc32(header);
    return SYS_OK;
}

status_t image_header_validate_basic(const image_header_t *header)
{
    if (header == 0) {
        return ERR_INVALID_ARG;
    }
    if (header->magic != IMAGE_MAGIC ||
        header->header_version != 1u ||
        header->header_size != sizeof(image_header_t) ||
        !fixed_string_nonempty_and_terminated(header->target_id, sizeof(header->target_id)) ||
        !fixed_string_nonempty_and_terminated(header->app_version, sizeof(header->app_version)) ||
        !fixed_string_nonempty_and_terminated(header->git_sha, sizeof(header->git_sha)) ||
        !fixed_string_nonempty_and_terminated(header->min_bootloader_version,
                                              sizeof(header->min_bootloader_version)) ||
        strcmp(header->target_id, PROJECT_TARGET_ID) != 0 ||
        header->image_size == 0u ||
        !sha256_nonzero(header->image_sha256)) {
        return ERR_IMAGE_INVALID;
    }
    if (image_header_crc32(header) != header->header_crc32) {
        return ERR_CRC;
    }

    return SYS_OK;
}

status_t image_header_validate_for_slot(const image_header_t *header, app_slot_t expected_slot)
{
    const partition_t *image_partition;
    status_t status;

    if (header == 0) {
        return ERR_INVALID_ARG;
    }
    status = image_header_validate_basic(header);
    if (status != SYS_OK) {
        return status;
    }
    if (image_header_get_state(header) == IMAGE_STATE_INVALID) {
        return ERR_IMAGE_INVALID;
    }
    if (header->target_slot != (uint32_t)expected_slot) {
        return ERR_SLOT_MISMATCH;
    }

    image_partition = partition_get_slot_image(expected_slot);
    if (image_partition == 0 || header->link_address != image_partition->start) {
        return ERR_SLOT_MISMATCH;
    }
    if (header->image_offset != (uint32_t)sizeof(image_header_t) ||
        (header->image_size & 1u) != 0u ||
        header->image_size > image_partition->size) {
        return ERR_IMAGE_INVALID;
    }

    return SYS_OK;
}

status_t image_header_validate_package_layout(const image_header_t *header, size_t package_size)
{
    uint32_t package_end_u32;
    uintmax_t package_end_wide;
    size_t package_end;

    if (header == 0) {
        return ERR_INVALID_ARG;
    }
    if (header->image_offset != (uint32_t)sizeof(image_header_t) ||
        header->image_offset > UINT32_MAX - header->image_size) {
        return ERR_IMAGE_INVALID;
    }
    package_end_wide = (uintmax_t)header->image_offset +
                       (uintmax_t)header->image_size;
    if (package_end_wide > (uintmax_t)SIZE_MAX) {
        return ERR_IMAGE_INVALID;
    }

    package_end_u32 = header->image_offset + header->image_size;
    package_end = (size_t)package_end_wide;
    if (package_end != (size_t)package_end_u32 || package_size != package_end) {
        return ERR_IMAGE_INVALID;
    }

    return SYS_OK;
}

image_state_t image_header_get_state(const image_header_t *header)
{
    if (header == 0) {
        return IMAGE_STATE_INVALID;
    }
    if (header->image_flags == IMAGE_FLAG_CANDIDATE) {
        return IMAGE_STATE_CANDIDATE;
    }
    if (header->image_flags == IMAGE_FLAG_CONFIRMED) {
        return IMAGE_STATE_CONFIRMED;
    }
    return IMAGE_STATE_INVALID;
}
