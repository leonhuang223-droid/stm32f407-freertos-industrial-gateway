#ifndef CRC32_H
#define CRC32_H

#include "error_code.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Streaming CRC32 calculation context. */
typedef struct {
    uint32_t state; /**< Reflected CRC32 accumulator before final xor. */
} crc32_context_t;

/**
 * @brief Initialize a CRC32 context.
 * @param ctx Context to initialize.
 * @return SYS_OK or ERR_INVALID_ARG.
 */
status_t crc32_init(crc32_context_t *ctx);
/**
 * @brief Feed bytes into a CRC32 context.
 * @param ctx Initialized context.
 * @param data Input bytes; may be NULL only when length is zero.
 * @param length Number of bytes to process.
 * @return SYS_OK or ERR_INVALID_ARG.
 */
status_t crc32_update(crc32_context_t *ctx, const uint8_t *data, size_t length);
/**
 * @brief Finish CRC32 calculation without modifying the context.
 * @param ctx Initialized context.
 * @param out_crc Destination for final CRC32.
 * @return SYS_OK or ERR_INVALID_ARG.
 */
status_t crc32_final(const crc32_context_t *ctx, uint32_t *out_crc);
/**
 * @brief Compute CRC32 over one contiguous buffer.
 * @param data Input bytes; may be NULL only when length is zero.
 * @param length Number of bytes to process.
 * @param out_crc Destination for final CRC32.
 * @return SYS_OK or ERR_INVALID_ARG.
 */
status_t crc32_compute(const uint8_t *data, size_t length, uint32_t *out_crc);

#ifdef __cplusplus
}
#endif

#endif
