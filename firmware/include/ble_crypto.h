#ifndef BLE_CRYPTO_H
#define BLE_CRYPTO_H

#include <stdint.h>
#include <stdbool.h>

/* ============================================================
 * BLE Crypto Module — AES-128 + HMAC-SHA256 + ECDH
 *
 * T-2026-00053: App对接联调
 * ============================================================ */

#define AES_KEY_SIZE    16
#define HMAC_SIZE_SHA256 32
#define ECDH_PUB_KEY_SIZE 32

/* Session context */
typedef struct {
    uint8_t session_key[AES_KEY_SIZE];
    bool key_valid;
    uint8_t ecdh_public_key[ECDH_PUB_KEY_SIZE];
    uint8_t ecdh_private_key[ECDH_PUB_KEY_SIZE];
} ble_crypto_ctx_t;

/* API */
void ble_crypto_init(ble_crypto_ctx_t *ctx);
bool ble_crypto_derive_session_key(ble_crypto_ctx_t *ctx, const uint8_t peer_public_key[ECDH_PUB_KEY_SIZE]);
bool ble_crypto_aes128_encrypt(const uint8_t key[AES_KEY_SIZE], const uint8_t *plaintext, uint16_t len, uint8_t *ciphertext);
bool ble_crypto_aes128_decrypt(const uint8_t key[AES_KEY_SIZE], const uint8_t *ciphertext, uint16_t len, uint8_t *plaintext);
bool ble_crypto_hmac_sha256(const uint8_t *key, uint16_t key_len, const uint8_t *msg, uint16_t msg_len, uint8_t *hmac_out);
bool ble_crypto_verify_hmac(const uint8_t *key, uint16_t key_len, const uint8_t *msg, uint16_t msg_len, const uint8_t *expected_hmac);

#endif /* BLE_CRYPTO_H */
