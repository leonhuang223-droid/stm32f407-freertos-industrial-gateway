#include "crc32.h"

#define CRC32_INITIAL_STATE 0xFFFFFFFFu
#define CRC32_FINAL_XOR 0xFFFFFFFFu
#define CRC32_POLYNOMIAL 0xEDB88320u

status_t crc32_init(crc32_context_t *ctx)
{
    if (ctx == 0) {
        return ERR_INVALID_ARG;
    }

    ctx->state = CRC32_INITIAL_STATE;
    return SYS_OK;
}

status_t crc32_update(crc32_context_t *ctx, const uint8_t *data, size_t length)
{
    size_t i;

    if (ctx == 0 || (data == 0 && length != 0u)) {
        return ERR_INVALID_ARG;
    }

    for (i = 0u; i < length; ++i) {
        int bit;

        ctx->state ^= data[i];
        for (bit = 0; bit < 8; ++bit) {
            if ((ctx->state & 1u) != 0u) {
                ctx->state = (ctx->state >> 1) ^ CRC32_POLYNOMIAL;
            } else {
                ctx->state >>= 1;
            }
        }
    }

    return SYS_OK;
}

status_t crc32_final(const crc32_context_t *ctx, uint32_t *out_crc)
{
    if (ctx == 0 || out_crc == 0) {
        return ERR_INVALID_ARG;
    }

    *out_crc = ctx->state ^ CRC32_FINAL_XOR;
    return SYS_OK;
}

status_t crc32_compute(const uint8_t *data, size_t length, uint32_t *out_crc)
{
    crc32_context_t ctx;
    status_t status;

    if (out_crc == 0 || (data == 0 && length != 0u)) {
        return ERR_INVALID_ARG;
    }

    status = crc32_init(&ctx);
    if (status != SYS_OK) {
        return status;
    }
    status = crc32_update(&ctx, data, length);
    if (status != SYS_OK) {
        return status;
    }
    return crc32_final(&ctx, out_crc);
}
