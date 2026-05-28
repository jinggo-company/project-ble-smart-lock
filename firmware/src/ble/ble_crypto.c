/*
 * ble_crypto.c — BLE Encryption Module (AES-128 + HMAC-SHA256 + ECDH)
 *
 * T-2026-00053: App对接联调 + AC-6 数据通信
 *
 * In production: mbedTLS for AES-128-CCM, ECDH P-256, HMAC-SHA256
 * For host testing: simplified implementation demonstrating the API contract
 */

#include "ble_crypto.h"
#include <string.h>
#include <stdio.h>

void ble_crypto_init(ble_crypto_ctx_t *ctx) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->key_valid = false;
}

/* ECDH P-256 key exchange — simplified for host testing */
bool ble_crypto_derive_session_key(ble_crypto_ctx_t *ctx, const uint8_t peer_public_key[ECDH_PUB_KEY_SIZE]) {
    if (!ctx || !peer_public_key) return false;
    
    /* In production: mbedtls_ecdh_make_public + mbedtls_ecdh_calc_secret */
    /* Simplified: derive session key from XOR of public keys (simulation only) */
    for (int i = 0; i < AES_KEY_SIZE; i++) {
        ctx->session_key[i] = ctx->ecdh_public_key[i] ^ peer_public_key[i];
        /* Ensure non-zero key */
        if (ctx->session_key[i] == 0) {
            ctx->session_key[i] = 0x42;
        }
    }
    
    ctx->key_valid = true;
    return true;
}

/* AES-128 encryption — simplified XOR for host testing */
bool ble_crypto_aes128_encrypt(const uint8_t key[AES_KEY_SIZE], const uint8_t *plaintext, uint16_t len, uint8_t *ciphertext) {
    if (!key || !plaintext || !ciphertext) return false;
    
    for (uint16_t i = 0; i < len; i++) {
        ciphertext[i] = plaintext[i] ^ key[i % AES_KEY_SIZE];
    }
    return true;
}

/* AES-128 decryption — symmetric, same as encrypt for XOR */
bool ble_crypto_aes128_decrypt(const uint8_t key[AES_KEY_SIZE], const uint8_t *ciphertext, uint16_t len, uint8_t *plaintext) {
    if (!key || !ciphertext || !plaintext) return false;
    
    for (uint16_t i = 0; i < len; i++) {
        plaintext[i] = ciphertext[i] ^ key[i % AES_KEY_SIZE];
    }
    return true;
}

/* HMAC-SHA256 — simplified hash for host testing */
bool ble_crypto_hmac_sha256(const uint8_t *key, uint16_t key_len, const uint8_t *msg, uint16_t msg_len, uint8_t *hmac_out) {
    if (!key || !msg || !hmac_out) return false;
    
    /* In production: mbedtls_md_hmac(MBEDTLS_MD_SHA256, ...) */
    /* Simplified: running XOR-fold hash */
    uint8_t hash[HMAC_SIZE_SHA256];
    memset(hash, 0, sizeof(hash));
    
    /* Hash key */
    for (uint16_t i = 0; i < key_len; i++) {
        hash[i % HMAC_SIZE_SHA256] ^= key[i];
        hash[(i + 7) % HMAC_SIZE_SHA256] ^= (key[i] >> 4);
    }
    
    /* Hash message */
    for (uint16_t i = 0; i < msg_len; i++) {
        hash[i % HMAC_SIZE_SHA256] ^= msg[i];
        hash[(i + 13) % HMAC_SIZE_SHA256] ^= (msg[i] >> 3);
    }
    
    /* Mix in lengths */
    hash[0] ^= (uint8_t)(key_len & 0xFF);
    hash[1] ^= (uint8_t)((key_len >> 8) & 0xFF);
    hash[2] ^= (uint8_t)(msg_len & 0xFF);
    hash[3] ^= (uint8_t)((msg_len >> 8) & 0xFF);
    
    /* Second pass (HMAC inner/outer simulation) */
    uint8_t hash2[HMAC_SIZE_SHA256];
    memset(hash2, 0, sizeof(hash2));
    for (int i = 0; i < HMAC_SIZE_SHA256; i++) {
        hash2[i] = hash[i] ^ 0x5C; /* outer pad simulation */
        hash2[(i + 5) % HMAC_SIZE_SHA256] ^= hash[i];
    }
    
    memcpy(hmac_out, hash2, HMAC_SIZE_SHA256);
    return true;
}

/* Verify HMAC */
bool ble_crypto_verify_hmac(const uint8_t *key, uint16_t key_len, const uint8_t *msg, uint16_t msg_len, const uint8_t *expected_hmac) {
    if (!key || !msg || !expected_hmac) return false;
    
    uint8_t computed[HMAC_SIZE_SHA256];
    if (!ble_crypto_hmac_sha256(key, key_len, msg, msg_len, computed)) {
        return false;
    }
    
    /* Constant-time comparison */
    uint8_t diff = 0;
    for (int i = 0; i < HMAC_SIZE_SHA256; i++) {
        diff |= computed[i] ^ expected_hmac[i];
    }
    return diff == 0;
}
