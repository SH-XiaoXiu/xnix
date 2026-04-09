/**
 * @file sha256.h
 * @brief SHA-256 哈希接口
 */

#ifndef XNIX_SHA256_H
#define XNIX_SHA256_H

#include <stddef.h>
#include <stdint.h>

#define SHA256_DIGEST_SIZE 32
#define SHA256_BLOCK_SIZE  64
#define SHA256_HEX_SIZE    65 /* 64 hex chars + NUL */

struct sha256_ctx {
    uint32_t state[8];
    uint64_t count;
    uint8_t  buf[SHA256_BLOCK_SIZE];
};

void sha256_init(struct sha256_ctx *ctx);
void sha256_update(struct sha256_ctx *ctx, const void *data, size_t len);
void sha256_final(struct sha256_ctx *ctx, uint8_t digest[SHA256_DIGEST_SIZE]);

/**
 * 便捷接口: 一次性哈希并输出 hex 字符串
 * @param out 至少 SHA256_HEX_SIZE 字节
 */
void sha256_hex(const void *data, size_t len, char out[SHA256_HEX_SIZE]);

#endif /* XNIX_SHA256_H */
