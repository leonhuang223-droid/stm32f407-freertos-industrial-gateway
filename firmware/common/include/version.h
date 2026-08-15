#ifndef VERSION_H
#define VERSION_H

#include "error_code.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Bootloader semantic version encoded into OTA compatibility checks. */
#define BOOTLOADER_VERSION "1.0.0"

/**
 * @brief Compare dotted numeric version strings.
 * @param left Left version string.
 * @param right Right version string.
 * @param result Receives -1, 0, or 1 for left <, ==, or > right.
 * @return SYS_OK when both versions are parseable.
 */
status_t version_compare(const char *left, const char *right, int *result);

#ifdef __cplusplus
}
#endif

#endif
