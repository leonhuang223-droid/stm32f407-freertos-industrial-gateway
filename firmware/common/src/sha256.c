#include "sha256.h"

#include <string.h>

#define SHA256_BLOCK_SIZE 64u

static const uint32_t sha256_k[64] = {
    0x428A2F98u, 0x71374491u, 0xB5C0FBCFu, 0xE9B5DBA5u, 0x3956C25Bu,
    0x59F111F1u, 0x923F82A4u, 0xAB1C5ED5u, 0xD807AA98u, 0x12835B01u,
    0x243185BEu, 0x550C7DC3u, 0x72BE5D74u, 0x80DEB1FEu, 0x9BDC06A7u,
    0xC19BF174u, 0xE49B69C1u, 0xEFBE4786u, 0x0FC19DC6u, 0x240CA1CCu,
    0x2DE92C6Fu, 0x4A7484AAu, 0x5CB0A9DCu, 0x76F988DAu, 0x983E5152u,
    0xA831C66Du, 0xB00327C8u, 0xBF597FC7u, 0xC6E00BF3u, 0xD5A79147u,
    0x06CA6351u, 0x14292967u, 0x27B70A85u, 0x2E1B2138u, 0x4D2C6DFCu,
    0x53380D13u, 0x650A7354u, 0x766A0ABBu, 0x81C2C92Eu, 0x92722C85u,
    0xA2BFE8A1u, 0xA81A664Bu, 0xC24B8B70u, 0xC76C51A3u, 0xD192E819u,
    0xD6990624u, 0xF40E3585u, 0x106AA070u, 0x19A4C116u, 0x1E376C08u,
    0x2748774Cu, 0x34B0BCB5u, 0x391C0CB3u, 0x4ED8AA4Au, 0x5B9CCA4Fu,
    0x682E6FF3u, 0x748F82EEu, 0x78A5636Fu, 0x84C87814u, 0x8CC70208u,
    0x90BEFFFAu, 0xA4506CEBu, 0xBEF9A3F7u, 0xC67178F2u};

static uint32_t rotr32(uint32_t value, unsigned int bits)
{
    return (value >> bits) | (value << (32u - bits));
}

static uint32_t load_be32(const uint8_t bytes[4])
{
    return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) | (uint32_t)bytes[3];
}

static void store_be32(uint32_t value, uint8_t out[4])
{
    out[0] = (uint8_t)((value >> 24) & 0xFFu);
    out[1] = (uint8_t)((value >> 16) & 0xFFu);
    out[2] = (uint8_t)((value >> 8) & 0xFFu);
    out[3] = (uint8_t)(value & 0xFFu);
}

static void store_be64(uint64_t value, uint8_t out[8])
{
    out[0] = (uint8_t)((value >> 56) & 0xFFu);
    out[1] = (uint8_t)((value >> 48) & 0xFFu);
    out[2] = (uint8_t)((value >> 40) & 0xFFu);
    out[3] = (uint8_t)((value >> 32) & 0xFFu);
    out[4] = (uint8_t)((value >> 24) & 0xFFu);
    out[5] = (uint8_t)((value >> 16) & 0xFFu);
    out[6] = (uint8_t)((value >> 8) & 0xFFu);
    out[7] = (uint8_t)(value & 0xFFu);
}

static void sha256_transform(sha256_context_t *ctx,
                             const uint8_t block[SHA256_BLOCK_SIZE])
{
    uint32_t w[64];
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t d;
    uint32_t e;
    uint32_t f;
    uint32_t g;
    uint32_t h;
    size_t i;

    for (i = 0u; i < 16u; ++i) {
        w[i] = load_be32(&block[i * 4u]);
    }
    for (i = 16u; i < 64u; ++i) {
        uint32_t s0 = rotr32(w[i - 15u], 7u) ^ rotr32(w[i - 15u], 18u) ^
                      (w[i - 15u] >> 3);
        uint32_t s1 =
            rotr32(w[i - 2u], 17u) ^ rotr32(w[i - 2u], 19u) ^ (w[i - 2u] >> 10);
        w[i] = w[i - 16u] + s0 + w[i - 7u] + s1;
    }

    a = ctx->state[0];
    b = ctx->state[1];
    c = ctx->state[2];
    d = ctx->state[3];
    e = ctx->state[4];
    f = ctx->state[5];
    g = ctx->state[6];
    h = ctx->state[7];

    for (i = 0u; i < 64u; ++i) {
        uint32_t s1 = rotr32(e, 6u) ^ rotr32(e, 11u) ^ rotr32(e, 25u);
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t temp1 = h + s1 + ch + sha256_k[i] + w[i];
        uint32_t s0 = rotr32(a, 2u) ^ rotr32(a, 13u) ^ rotr32(a, 22u);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = s0 + maj;

        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
    ctx->state[5] += f;
    ctx->state[6] += g;
    ctx->state[7] += h;
}

static size_t min_size(size_t left, size_t right)
{
    return left < right ? left : right;
}

status_t sha256_init(sha256_context_t *ctx)
{
    if (ctx == 0) {
        return ERR_INVALID_ARG;
    }

    ctx->state[0] = 0x6A09E667u;
    ctx->state[1] = 0xBB67AE85u;
    ctx->state[2] = 0x3C6EF372u;
    ctx->state[3] = 0xA54FF53Au;
    ctx->state[4] = 0x510E527Fu;
    ctx->state[5] = 0x9B05688Cu;
    ctx->state[6] = 0x1F83D9ABu;
    ctx->state[7] = 0x5BE0CD19u;
    ctx->bit_count = 0u;
    ctx->buffer_len = 0u;
    memset(ctx->buffer, 0, sizeof(ctx->buffer));

    return SYS_OK;
}

status_t
sha256_update(sha256_context_t *ctx, const uint8_t *data, size_t length)
{
    size_t offset = 0u;

    if (ctx == 0 || (data == 0 && length != 0u)) {
        return ERR_INVALID_ARG;
    }
    if (length > (size_t)((UINT64_MAX - ctx->bit_count) / 8u)) {
        return ERR_INVALID_ARG;
    }

    ctx->bit_count += (uint64_t)length * 8u;

    if (ctx->buffer_len != 0u) {
        size_t take = min_size(length, SHA256_BLOCK_SIZE - ctx->buffer_len);

        if (take != 0u) {
            memcpy(&ctx->buffer[ctx->buffer_len], data, take);
            ctx->buffer_len += take;
            offset += take;
        }
        if (ctx->buffer_len == SHA256_BLOCK_SIZE) {
            sha256_transform(ctx, ctx->buffer);
            ctx->buffer_len = 0u;
        }
    }

    while (length - offset >= SHA256_BLOCK_SIZE) {
        sha256_transform(ctx, &data[offset]);
        offset += SHA256_BLOCK_SIZE;
    }

    if (offset < length) {
        ctx->buffer_len = length - offset;
        memcpy(ctx->buffer, &data[offset], ctx->buffer_len);
    }

    return SYS_OK;
}

status_t sha256_final(sha256_context_t *ctx,
                      uint8_t out_digest[SHA256_DIGEST_SIZE])
{
    uint8_t length_bytes[8];
    size_t i;

    if (ctx == 0 || out_digest == 0) {
        return ERR_INVALID_ARG;
    }

    store_be64(ctx->bit_count, length_bytes);

    ctx->buffer[ctx->buffer_len++] = 0x80u;
    if (ctx->buffer_len > 56u) {
        memset(&ctx->buffer[ctx->buffer_len],
               0,
               SHA256_BLOCK_SIZE - ctx->buffer_len);
        sha256_transform(ctx, ctx->buffer);
        ctx->buffer_len = 0u;
    }

    memset(&ctx->buffer[ctx->buffer_len], 0, 56u - ctx->buffer_len);
    memcpy(&ctx->buffer[56], length_bytes, sizeof(length_bytes));
    sha256_transform(ctx, ctx->buffer);

    for (i = 0u; i < 8u; ++i) {
        store_be32(ctx->state[i], &out_digest[i * 4u]);
    }

    memset(ctx, 0, sizeof(*ctx));
    return SYS_OK;
}

status_t sha256_compute(const uint8_t *data,
                        size_t length,
                        uint8_t out_digest[SHA256_DIGEST_SIZE])
{
    sha256_context_t ctx;
    status_t status;

    if (out_digest == 0 || (data == 0 && length != 0u)) {
        return ERR_INVALID_ARG;
    }

    status = sha256_init(&ctx);
    if (status != SYS_OK) {
        return status;
    }
    status = sha256_update(&ctx, data, length);
    if (status != SYS_OK) {
        return status;
    }
    return sha256_final(&ctx, out_digest);
}
