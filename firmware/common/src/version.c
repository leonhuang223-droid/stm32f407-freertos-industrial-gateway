#include "version.h"

#include <ctype.h>
#include <stddef.h>

static status_t parse_version_part(const char **cursor, unsigned int *value, int expect_dot)
{
    const char *p;
    unsigned int parsed = 0u;
    unsigned int digits = 0u;

    if (cursor == 0 || *cursor == 0 || value == 0) {
        return ERR_INVALID_ARG;
    }

    p = *cursor;
    while (*p >= '0' && *p <= '9') {
        parsed = parsed * 10u + (unsigned int)(*p - '0');
        if (parsed > 65535u) {
            return ERR_INVALID_ARG;
        }
        ++digits;
        ++p;
    }

    if (digits == 0u) {
        return ERR_INVALID_ARG;
    }

    if (expect_dot) {
        if (*p != '.') {
            return ERR_INVALID_ARG;
        }
        ++p;
    } else if (*p != '\0') {
        return ERR_INVALID_ARG;
    }

    *cursor = p;
    *value = parsed;
    return SYS_OK;
}

static status_t parse_version(const char *text, unsigned int out[3])
{
    const char *cursor = text;
    status_t status;

    if (text == 0 || out == 0) {
        return ERR_INVALID_ARG;
    }

    status = parse_version_part(&cursor, &out[0], 1);
    if (status != SYS_OK) {
        return status;
    }
    status = parse_version_part(&cursor, &out[1], 1);
    if (status != SYS_OK) {
        return status;
    }
    return parse_version_part(&cursor, &out[2], 0);
}

status_t version_compare(const char *left, const char *right, int *result)
{
    unsigned int left_parts[3];
    unsigned int right_parts[3];
    status_t status;
    int i;

    if (result != 0) {
        *result = 0;
    }

    if (left == 0 || right == 0 || result == 0) {
        return ERR_INVALID_ARG;
    }

    status = parse_version(left, left_parts);
    if (status != SYS_OK) {
        return status;
    }
    status = parse_version(right, right_parts);
    if (status != SYS_OK) {
        return status;
    }

    for (i = 0; i < 3; ++i) {
        if (left_parts[i] < right_parts[i]) {
            *result = -1;
            return SYS_OK;
        }
        if (left_parts[i] > right_parts[i]) {
            *result = 1;
            return SYS_OK;
        }
    }

    *result = 0;
    return SYS_OK;
}
