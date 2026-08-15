#include "manifest.h"

#include "platform_constants.h"
#include "version.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static const char *skip_ws(const char *p)
{
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
        ++p;
    }
    return p;
}

typedef enum {
    MANIFEST_FIELD_VERSION = 0,
    MANIFEST_FIELD_TARGET_ID,
    MANIFEST_FIELD_APP_VERSION,
    MANIFEST_FIELD_TARGET_SLOT,
    MANIFEST_FIELD_LINK_ADDRESS,
    MANIFEST_FIELD_IMAGE_SIZE,
    MANIFEST_FIELD_CRC32,
    MANIFEST_FIELD_SHA256,
    MANIFEST_FIELD_DOWNLOAD_URL,
    MANIFEST_FIELD_MIN_BOOTLOADER_VERSION,
    MANIFEST_FIELD_FORCE_UPDATE,
    MANIFEST_FIELD_RELEASE_NOTE,
    MANIFEST_FIELD_COUNT
} manifest_field_id_t;

static int manifest_field_id(const char *key, manifest_field_id_t *out_id)
{
    struct field_map {
        const char *name;
        manifest_field_id_t id;
    };
    static const struct field_map fields[] = {
        { "manifest_version", MANIFEST_FIELD_VERSION },
        { "target_id", MANIFEST_FIELD_TARGET_ID },
        { "version", MANIFEST_FIELD_APP_VERSION },
        { "target_slot", MANIFEST_FIELD_TARGET_SLOT },
        { "link_address", MANIFEST_FIELD_LINK_ADDRESS },
        { "image_size", MANIFEST_FIELD_IMAGE_SIZE },
        { "crc32", MANIFEST_FIELD_CRC32 },
        { "sha256", MANIFEST_FIELD_SHA256 },
        { "download_url", MANIFEST_FIELD_DOWNLOAD_URL },
        { "min_bootloader_version", MANIFEST_FIELD_MIN_BOOTLOADER_VERSION },
        { "force_update", MANIFEST_FIELD_FORCE_UPDATE },
        { "release_note", MANIFEST_FIELD_RELEASE_NOTE }
    };
    size_t i;

    for (i = 0u; i < sizeof(fields) / sizeof(fields[0]); ++i) {
        if (strcmp(key, fields[i].name) == 0) {
            *out_id = fields[i].id;
            return 1;
        }
    }
    return 0;
}

static const char *parse_u32_value(const char *p, uint32_t *out)
{
    uint32_t value = 0u;
    unsigned int digits = 0u;

    if (p == 0 || out == 0 || *p < '0' || *p > '9') {
        return 0;
    }
    while (*p >= '0' && *p <= '9') {
        uint32_t digit = (uint32_t)(*p - '0');
        if (value > (UINT32_MAX - digit) / 10u) {
            return 0;
        }
        value = value * 10u + digit;
        ++digits;
        ++p;
    }
    if (digits == 0u) {
        return 0;
    }
    *out = value;
    return p;
}

static const char *parse_bool_value(const char *p, uint32_t *out)
{
    if (p == 0 || out == 0) {
        return 0;
    }
    if (strncmp(p, "true", 4u) == 0) {
        *out = 1u;
        return p + 4u;
    }
    if (strncmp(p, "false", 5u) == 0) {
        *out = 0u;
        return p + 5u;
    }
    return 0;
}

static const char *parse_json_string_value(const char *p,
                                           char *out,
                                           size_t capacity,
                                           int truncate)
{
    size_t count = 0u;

    if (p == 0 || out == 0 || capacity == 0u || *p != '"') {
        return 0;
    }
    ++p;
    memset(out, 0, capacity);
    while (*p != '\0' && *p != '"') {
        unsigned char ch = (unsigned char)*p;
        if (*p == '\\' || ch < 0x20u) {
            return 0;
        }
        if (count + 1u >= capacity) {
            if (!truncate) {
                return 0;
            }
        } else {
            out[count] = *p;
        }
        ++count;
        ++p;
    }
    if (*p != '"') {
        return 0;
    }
    return p + 1;
}

static int hex_value(char ch)
{
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
        return ch - 'a' + 10;
    }
    if (ch >= 'A' && ch <= 'F') {
        return ch - 'A' + 10;
    }
    return -1;
}

static const char *parse_sha256_value(const char *p, uint8_t out[IMAGE_SHA256_LEN])
{
    size_t i;

    if (p == 0 || out == 0 || *p != '"') {
        return 0;
    }
    ++p;
    for (i = 0u; i < IMAGE_SHA256_LEN; ++i) {
        int hi;
        int lo;

        if (*p == '\0' || *p == '"') {
            return 0;
        }
        hi = hex_value(*p);
        if (hi < 0) {
            return 0;
        }
        ++p;

        if (*p == '\0' || *p == '"') {
            return 0;
        }
        lo = hex_value(*p);
        if (lo < 0) {
            return 0;
        }
        out[i] = (uint8_t)((hi << 4) | lo);
        ++p;
    }
    if (*p != '"') {
        return 0;
    }
    return p + 1;
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

static int string_present(const char *text, size_t capacity)
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

static int is_http_url(const char *url)
{
    return strncmp(url, "http://", 7u) == 0 && url[7] != '\0' && url[7] != '/';
}

status_t manifest_parse_json(const char *json, ota_manifest_t *out_manifest)
{
    const char *p;
    uint32_t seen = 0u;
    uint32_t number;

    if (json == 0 || out_manifest == 0) {
        return ERR_INVALID_ARG;
    }

    memset(out_manifest, 0, sizeof(*out_manifest));

    p = skip_ws(json);
    if (*p != '{') {
        return ERR_IMAGE_INVALID;
    }
    p = skip_ws(p + 1);
    if (*p == '}') {
        return ERR_IMAGE_INVALID;
    }

    while (*p != '\0') {
        char key[MANIFEST_MIN_BOOTLOADER_VERSION_LEN + 8u];
        manifest_field_id_t id;
        uint32_t bit;

        p = parse_json_string_value(p, key, sizeof(key), 0);
        if (p == 0 || !manifest_field_id(key, &id)) {
            return ERR_IMAGE_INVALID;
        }
        bit = 1u << (uint32_t)id;
        if ((seen & bit) != 0u) {
            return ERR_IMAGE_INVALID;
        }
        seen |= bit;

        p = skip_ws(p);
        if (*p != ':') {
            return ERR_IMAGE_INVALID;
        }
        p = skip_ws(p + 1);

        switch (id) {
        case MANIFEST_FIELD_VERSION:
            p = parse_u32_value(p, &number);
            if (p == 0 || number != 1u) {
                return ERR_IMAGE_INVALID;
            }
            out_manifest->manifest_version = (uint16_t)number;
            break;
        case MANIFEST_FIELD_TARGET_ID:
            p = parse_json_string_value(p, out_manifest->target_id,
                                        sizeof(out_manifest->target_id), 0);
            break;
        case MANIFEST_FIELD_APP_VERSION:
            p = parse_json_string_value(p, out_manifest->version,
                                        sizeof(out_manifest->version), 0);
            break;
        case MANIFEST_FIELD_TARGET_SLOT:
            p = parse_u32_value(p, &number);
            if (p == 0 || (number != (uint32_t)SLOT_A && number != (uint32_t)SLOT_B)) {
                return ERR_IMAGE_INVALID;
            }
            if (p != 0) {
                out_manifest->target_slot = number;
            }
            break;
        case MANIFEST_FIELD_LINK_ADDRESS:
            p = parse_u32_value(p, &out_manifest->link_address);
            break;
        case MANIFEST_FIELD_IMAGE_SIZE:
            p = parse_u32_value(p, &out_manifest->image_size);
            break;
        case MANIFEST_FIELD_CRC32:
            p = parse_u32_value(p, &out_manifest->crc32);
            break;
        case MANIFEST_FIELD_SHA256:
            p = parse_sha256_value(p, out_manifest->sha256);
            break;
        case MANIFEST_FIELD_DOWNLOAD_URL:
            p = parse_json_string_value(p, out_manifest->download_url,
                                        sizeof(out_manifest->download_url), 0);
            break;
        case MANIFEST_FIELD_MIN_BOOTLOADER_VERSION:
            p = parse_json_string_value(p, out_manifest->min_bootloader_version,
                                        sizeof(out_manifest->min_bootloader_version), 0);
            break;
        case MANIFEST_FIELD_FORCE_UPDATE:
            p = parse_bool_value(p, &out_manifest->force_update);
            break;
        case MANIFEST_FIELD_RELEASE_NOTE:
            p = parse_json_string_value(p, out_manifest->release_note,
                                        sizeof(out_manifest->release_note), 1);
            break;
        default:
            return ERR_IMAGE_INVALID;
        }
        if (p == 0) {
            return ERR_IMAGE_INVALID;
        }

        p = skip_ws(p);
        if (*p == ',') {
            p = skip_ws(p + 1);
            if (*p == '}') {
                return ERR_IMAGE_INVALID;
            }
            continue;
        }
        if (*p == '}') {
            p = skip_ws(p + 1);
            if (*p != '\0') {
                return ERR_IMAGE_INVALID;
            }
            break;
        }
        return ERR_IMAGE_INVALID;
    }
    if (seen != ((1u << MANIFEST_FIELD_COUNT) - 1u)) {
        return ERR_IMAGE_INVALID;
    }
    if (!is_http_url(out_manifest->download_url)) {
        return ERR_IMAGE_INVALID;
    }

    return SYS_OK;
}

status_t manifest_validate(const ota_manifest_t *manifest, const manifest_validate_context_t *context)
{
    const partition_t *partition;
    int compare_result = 0;
    status_t status;

    if (manifest == 0 || context == 0 ||
        context->target_id == 0 ||
        context->current_version == 0 ||
        context->current_bootloader_version == 0) {
        return ERR_INVALID_ARG;
    }
    if (!string_present(manifest->target_id, sizeof(manifest->target_id)) ||
        !string_present(manifest->version, sizeof(manifest->version)) ||
        !string_present(manifest->download_url, sizeof(manifest->download_url)) ||
        !string_present(manifest->min_bootloader_version, sizeof(manifest->min_bootloader_version)) ||
        strcmp(manifest->target_id, context->target_id) != 0 ||
        manifest->manifest_version != 1u ||
        manifest->image_size == 0u ||
        !sha256_nonzero(manifest->sha256) ||
        !is_http_url(manifest->download_url) ||
        (manifest->force_update != 0u && manifest->force_update != 1u)) {
        return ERR_IMAGE_INVALID;
    }

    if (manifest->target_slot != (uint32_t)SLOT_A && manifest->target_slot != (uint32_t)SLOT_B) {
        return ERR_SLOT_MISMATCH;
    }

    partition = partition_get_slot_image((app_slot_t)manifest->target_slot);
    if (partition == 0 || manifest->link_address != partition->start) {
        return ERR_SLOT_MISMATCH;
    }
    if (manifest->image_size < IMAGE_PACKAGE_V1_MIN_SIZE ||
        manifest->image_size > IMAGE_PACKAGE_V1_MAX_SIZE ||
        (manifest->image_size & 1u) != 0u) {
        return ERR_IMAGE_INVALID;
    }

    status = version_compare(manifest->version, context->current_version, &compare_result);
    if (status != SYS_OK) {
        return ERR_IMAGE_INVALID;
    }
    if (compare_result <= 0 && !manifest->force_update) {
        return ERR_IMAGE_INVALID;
    }

    status = version_compare(manifest->min_bootloader_version,
                             context->current_bootloader_version,
                             &compare_result);
    if (status != SYS_OK) {
        return ERR_IMAGE_INVALID;
    }
    if (compare_result > 0) {
        return ERR_UNSUPPORTED;
    }

    return SYS_OK;
}
