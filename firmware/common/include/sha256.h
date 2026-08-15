#ifndef SHA256_H
#define SHA256_H

#include "error_code.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Size in bytes of a SHA256 digest. */
#define SHA256_DIGEST_SIZE 32

/** Streaming SHA256 calculation context. */
typedef struct {
    uint32_t state[8]; /**< Working hash state words. */
    uint64_t bit_count; /**< Total input length in bits. */
    uint8_t buffer[64]; /**< Partial block buffer. */
    size_t buffer_len; /**< Number of valid bytes in buffer. */
} sha256_context_t;

/**
 * @brief Initialize a SHA256 context.
 * @param ctx Context to initialize.
 * @return SYS_OK or ERR_INVALID_ARG.
 */
status_t sha256_init(sha256_context_t *ctx);
/**
 * @brief Feed bytes into a SHA256 context.
 * @param ctx Initialized context.
 * @param data Input bytes; may be NULL only when length is zero.
 * @param length Number of bytes to process.
 * @return SYS_OK or ERR_INVALID_ARG.
 */
status_t sha256_update(sha256_context_t *ctx, const uint8_t *data, size_t length);
/**
 * @brief Finish SHA256 calculation.
 * @param ctx Initialized context; finalized padding mutates it.
 * @param out_digest Destination for SHA256_DIGEST_SIZE bytes.
 * @return SYS_OK or ERR_INVALID_ARG.
 */
status_t sha256_final(sha256_context_t *ctx, uint8_t out_digest[SHA256_DIGEST_SIZE]);
/**
 * @brief Compute SHA256 over one contiguous buffer.
 * @param data Input bytes; may be NULL only when length is zero.
 * @param length Number of bytes to process.
 * @param out_digest Destination for SHA256_DIGEST_SIZE bytes.
 * @return SYS_OK or ERR_INVALID_ARG.
 */
status_t sha256_compute(const uint8_t *data, size_t length,
                        uint8_t out_digest[SHA256_DIGEST_SIZE]);

#ifdef __cplusplus
}
#endif

#endif
