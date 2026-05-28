/*
 * hmac_verify.c — HMAC-SHA256 Verification Module
 *
 * T-2026-00053: App对接联调 + AC-6 数据通信
 */

#include "hmac_verify.h"
#include <string.h>

void hmac_verify_init(hmac_verify_ctx_t *ctx) {
    memset(ctx, 0, sizeof(*ctx));
}

bool hmac_verify_set_key(hmac_verify_ctx_t *ctx, const uint8_t *key, uint16_t len) {
    if (!ctx || !key || len == 0) return false;
    uint16_t copy_len = (len < HMAC_KEY_SIZE) ? len : HMAC_KEY_SIZE;
    memcpy(ctx->key, key, copy_len);
    ctx->key_set = true;
    return true;
}

/* Simplified HMAC computation for host testing */
bool hmac_verify_compute(const hmac_verify_ctx_t *ctx, const uint8_t *data, uint16_t data_len, uint8_t *hmac_out, uint16_t hmac_out_size) {
    if (!ctx || !ctx->key_set || !data || !hmac_out || hmac_out_size == 0) return false;
    
    /* Running XOR-fold hash (simulation of HMAC-SHA256) */
    uint8_t hash[32];
    memset(hash, 0, sizeof(hash));
    
    for (uint16_t i = 0; i < HMAC_KEY_SIZE; i++) {
        hash[i % 32] ^= ctx->key[i];
        hash[(i + 7) % 32] ^= (ctx->key[i] >> 4);
    }
    
    for (uint16_t i = 0; i < data_len; i++) {
        hash[i % 32] ^= data[i];
        hash[(i + 13) % 32] ^= (data[i] >> 3);
    }
    
    /* Second pass */
    uint8_t hash2[32];
    memset(hash2, 0, sizeof(hash2));
    for (int i = 0; i < 32; i++) {
        hash2[i] = hash[i] ^ 0x5C;
        hash2[(i + 5) % 32] ^= hash[i];
    }
    
    uint16_t copy = (hmac_out_size < 32) ? hmac_out_size : 32;
    memcpy(hmac_out, hash2, copy);
    return true;
}

bool hmac_verify_check(const hmac_verify_ctx_t *ctx, const uint8_t *data, uint16_t data_len, const uint8_t *expected_hmac, uint16_t expected_hmac_len) {
    if (!ctx || !expected_hmac || expected_hmac_len == 0) return false;
    
    uint8_t computed[32];
    if (!hmac_verify_compute(ctx, data, data_len, computed, sizeof(computed))) {
        return false;
    }
    
    /* Constant-time compare */
    uint8_t diff = 0;
    uint16_t cmp_len = (expected_hmac_len < 32) ? expected_hmac_len : 32;
    for (uint16_t i = 0; i < cmp_len; i++) {
        diff |= computed[i] ^ expected_hmac[i];
    }
    return diff == 0;
}
