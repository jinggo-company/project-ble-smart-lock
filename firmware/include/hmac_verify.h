#ifndef HMAC_VERIFY_H
#define HMAC_VERIFY_H

#include <stdint.h>
#include <stdbool.h>

/* ============================================================
 * HMAC Verify Module — HMAC-SHA256 message authentication
 *
 * T-2026-00053: App对接联调 + AC-6 数据通信
 * ============================================================ */

#define HMAC_KEY_SIZE    16
#define HMAC_TRUNCATED_SIZE 4  /* We use first 4 bytes of HMAC-SHA256 in BLE frames */

typedef struct {
    uint8_t key[HMAC_KEY_SIZE];
    bool key_set;
} hmac_verify_ctx_t;

void hmac_verify_init(hmac_verify_ctx_t *ctx);
bool hmac_verify_set_key(hmac_verify_ctx_t *ctx, const uint8_t *key, uint16_t len);
bool hmac_verify_compute(const hmac_verify_ctx_t *ctx, const uint8_t *data, uint16_t data_len, uint8_t *hmac_out, uint16_t hmac_out_size);
bool hmac_verify_check(const hmac_verify_ctx_t *ctx, const uint8_t *data, uint16_t data_len, const uint8_t *expected_hmac, uint16_t expected_hmac_len);

#endif /* HMAC_VERIFY_H */
