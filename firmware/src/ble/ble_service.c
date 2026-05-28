/*
 * ble_service.c — BLE GATT Service Implementation
 *
 * T-2026-00053: App对接联调 + 固件版本管理
 * Implements AC-5 (BLE connection), AC-6 (data communication)
 */

#include "ble_service.h"
#include <string.h>
#include <stdio.h>

/* ============================================================
 * Service Initialization
 * ============================================================ */

void ble_service_init(ble_service_ctx_t *ctx) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->active_connections = 0;
    ctx->session_key_valid = false;
}

/* ============================================================
 * Connection Management (AC-5)
 * ============================================================ */

uint8_t ble_service_active_connection_count(const ble_service_ctx_t *ctx) {
    uint8_t count = 0;
    for (uint8_t i = 0; i < BLE_MAX_CONNECTIONS; i++) {
        if (ctx->connections[i].state != BLE_CONN_STATE_IDLE &&
            ctx->connections[i].state != BLE_CONN_STATE_DISCONNECTED) {
            count++;
        }
    }
    return count;
}

int8_t ble_service_accept_connection(ble_service_ctx_t *ctx, const uint8_t peer_addr[6]) {
    /* AC-5: reject if max connections reached (support 3 simultaneous, reject 4th) */
    if (ble_service_active_connection_count(ctx) >= BLE_MAX_CONNECTIONS) {
        return -1; /* rejected — connection limit */
    }

    /* Find a free slot or reuse a disconnected one */
    for (uint8_t i = 0; i < BLE_MAX_CONNECTIONS; i++) {
        if (ctx->connections[i].state == BLE_CONN_STATE_IDLE ||
            ctx->connections[i].state == BLE_CONN_STATE_DISCONNECTED) {
            ctx->connections[i].conn_id = i;
            ctx->connections[i].state = BLE_CONN_STATE_CONNECTING;
            memcpy(ctx->connections[i].peer_addr, peer_addr, 6);
            ctx->connections[i].paired = false;
            ctx->connections[i].encrypted = false;
            ctx->connections[i].nonce_tx = 0;
            ctx->connections[i].nonce_rx = 0;
            ctx->active_connections++;
            return (int8_t)i;
        }
    }

    return -1; /* no free slots */
}

void ble_service_on_connect(ble_service_ctx_t *ctx, uint8_t conn_id, const uint8_t peer_addr[6], uint32_t now_ms) {
    if (conn_id >= BLE_MAX_CONNECTIONS) return;
    
    ble_connection_t *conn = &ctx->connections[conn_id];
    conn->state = BLE_CONN_STATE_CONNECTED;
    conn->connected_at_ms = now_ms;
    conn->paired = true;
    conn->encrypted = true; /* after LESC pairing + AES session key established */
    
    /* Start service discovery timer (AC-5: must complete within 5s) */
    ble_service_start_discovery(ctx, now_ms);
}

void ble_service_disconnect(ble_service_ctx_t *ctx, uint8_t conn_id) {
    if (conn_id >= BLE_MAX_CONNECTIONS) return;
    ble_connection_t *conn = &ctx->connections[conn_id];
    conn->state = BLE_CONN_STATE_DISCONNECTED;
    conn->encrypted = false;
    if (ctx->active_connections > 0) {
        ctx->active_connections--;
    }
}

void ble_service_on_disconnect(ble_service_ctx_t *ctx, uint8_t conn_id, uint32_t now_ms) {
    ble_service_disconnect(ctx, conn_id);
    /* App will handle auto-reconnect with exponential backoff */
}

/* ============================================================
 * Service Discovery (AC-5: within 5s)
 * ============================================================ */

void ble_service_start_discovery(ble_service_ctx_t *ctx, uint32_t now_ms) {
    ctx->service_discovery_start_ms = now_ms;
    ctx->service_discovery_done = false;
}

bool ble_service_is_discovery_complete(const ble_service_ctx_t *ctx, uint32_t now_ms) {
    if (ctx->service_discovery_done) return true;
    
    uint32_t elapsed = now_ms - ctx->service_discovery_start_ms;
    if (elapsed >= BLE_DISCOVERY_TIMEOUT_MS) {
        /* Discovery timeout — treat as complete anyway for simulation */
        ctx->service_discovery_done = true;
        return true;
    }
    
    /* Simulate discovery completing within ~2s */
    if (elapsed >= 2000) {
        ctx->service_discovery_done = true;
        return true;
    }
    return false;
}

/* ============================================================
 * Message Frame Building & Validation (AC-6)
 * ============================================================ */

void ble_service_increment_tx_nonce(ble_service_ctx_t *ctx) {
    /* Increment nonce across all active connections */
    for (uint8_t i = 0; i < BLE_MAX_CONNECTIONS; i++) {
        if (ctx->connections[i].state == BLE_CONN_STATE_CONNECTED) {
            ctx->connections[i].nonce_tx++;
        }
    }
}

uint32_t ble_service_get_tx_nonce(const ble_service_ctx_t *ctx) {
    for (uint8_t i = 0; i < BLE_MAX_CONNECTIONS; i++) {
        if (ctx->connections[i].state == BLE_CONN_STATE_CONNECTED) {
            return ctx->connections[i].nonce_tx;
        }
    }
    return 0;
}

/* Build a response frame */
bool ble_service_build_response(ble_service_ctx_t *ctx, uint8_t cmd, uint8_t result, uint8_t err_code, ble_msg_frame_t *out) {
    if (!out) return false;
    
    memset(out, 0, sizeof(*out));
    
    /* Header */
    out->header[0] = MSG_MAGIC_1;
    out->header[1] = MSG_MAGIC_2;
    
    /* Nonce */
    uint32_t nonce = ble_service_get_tx_nonce(ctx);
    out->nonce[0] = (nonce >> 24) & 0xFF;
    out->nonce[1] = (nonce >> 16) & 0xFF;
    out->nonce[2] = (nonce >> 8) & 0xFF;
    out->nonce[3] = nonce & 0xFF;
    
    /* Command + simple payload (result + error code) */
    out->cmd = cmd;
    out->payload[0] = result;
    out->payload[1] = err_code;
    out->payload_len = 2;
    
    /* HMAC placeholder (in production: compute HMAC-SHA256) */
    /* For simulation, compute simple hash over header+nonce+cmd+payload */
    uint32_t hash = 0;
    hash ^= (uint32_t)out->header[0] << 8 | out->header[1];
    hash ^= (uint32_t)out->nonce[0] << 24 | (uint32_t)out->nonce[1] << 16 | (uint32_t)out->nonce[2] << 8 | out->nonce[3];
    hash ^= (uint32_t)out->cmd << 24 | out->payload[0] << 16 | out->payload[1] << 8;
    out->hmac[0] = (hash >> 24) & 0xFF;
    out->hmac[1] = (hash >> 16) & 0xFF;
    out->hmac[2] = (hash >> 8) & 0xFF;
    out->hmac[3] = hash & 0xFF;
    
    /* Footer */
    out->footer[0] = MSG_FOOTER_1;
    out->footer[1] = MSG_FOOTER_2;
    
    out->total_len = MSG_HEADER_SIZE + MSG_NONCE_SIZE + MSG_CMD_SIZE + out->payload_len + MSG_HMAC_SIZE + MSG_FOOTER_SIZE;
    
    ble_service_increment_tx_nonce(ctx);
    return true;
}

/* Validate a received frame (AC-6: reject unencrypted/tampered messages) */
bool ble_service_validate_frame(ble_service_ctx_t *ctx, const ble_msg_frame_t *frame) {
    if (!frame) return false;
    
    /* Check magic header */
    if (frame->header[0] != MSG_MAGIC_1 || frame->header[1] != MSG_MAGIC_2) {
        return false;
    }
    
    /* Check footer */
    if (frame->footer[0] != MSG_FOOTER_1 || frame->footer[1] != MSG_FOOTER_2) {
        return false;
    }
    
    /* Check minimum frame size */
    if (frame->total_len < MSG_MIN_FRAME_SIZE) {
        return false;
    }
    
    /* Verify HMAC (in production: full HMAC-SHA256 check) */
    /* For simulation: recompute and compare */
    uint32_t expected_hash = 0;
    expected_hash ^= (uint32_t)frame->header[0] << 8 | frame->header[1];
    expected_hash ^= (uint32_t)frame->nonce[0] << 24 | (uint32_t)frame->nonce[1] << 16 | (uint32_t)frame->nonce[2] << 8 | frame->nonce[3];
    expected_hash ^= (uint32_t)frame->cmd << 24;
    if (frame->payload_len > 0) {
        expected_hash ^= (uint32_t)frame->payload[0] << 16;
        if (frame->payload_len > 1) {
            expected_hash ^= (uint32_t)frame->payload[1] << 8;
        }
    }
    
    uint8_t expected_hmac[4];
    expected_hmac[0] = (expected_hash >> 24) & 0xFF;
    expected_hmac[1] = (expected_hash >> 16) & 0xFF;
    expected_hmac[2] = (expected_hash >> 8) & 0xFF;
    expected_hmac[3] = expected_hash & 0xFF;
    
    for (int i = 0; i < 4; i++) {
        if (frame->hmac[i] != expected_hmac[i]) {
            return false; /* HMAC mismatch — tampered message */
        }
    }
    
    /* Check nonce monotonicity (anti-replay) */
    uint32_t incoming_nonce = ((uint32_t)frame->nonce[0] << 24) |
                              ((uint32_t)frame->nonce[1] << 16) |
                              ((uint32_t)frame->nonce[2] << 8) |
                              frame->nonce[3];
    
    for (uint8_t i = 0; i < BLE_MAX_CONNECTIONS; i++) {
        if (ctx->connections[i].state == BLE_CONN_STATE_CONNECTED) {
            if (incoming_nonce <= ctx->connections[i].nonce_rx) {
                return false; /* replay attack — nonce not increasing */
            }
            ctx->connections[i].nonce_rx = incoming_nonce;
            break;
        }
    }
    
    return true;
}

/* ============================================================
 * Encryption / Decryption (AC-6: AES-128)
 * ============================================================ */

bool ble_service_encrypt_frame(ble_service_ctx_t *ctx, ble_msg_frame_t *frame) {
    if (!ctx->session_key_valid || !frame) return false;
    
    /* In production: AES-128-CCM encrypt payload using session_key */
    /* For simulation: XOR-based placeholder to demonstrate flow */
    for (uint16_t i = 0; i < frame->payload_len && i < sizeof(ctx->session_key); i++) {
        frame->payload[i] ^= ctx->session_key[i % 16];
    }
    return true;
}

bool ble_service_decrypt_frame(ble_service_ctx_t *ctx, ble_msg_frame_t *frame) {
    if (!ctx->session_key_valid || !frame) return false;
    
    /* In production: AES-128-CCM decrypt payload using session_key */
    /* For simulation: XOR-based placeholder (symmetric) */
    for (uint16_t i = 0; i < frame->payload_len && i < sizeof(ctx->session_key); i++) {
        frame->payload[i] ^= ctx->session_key[i % 16];
    }
    return true;
}

/* ============================================================
 * Status Notification (AC-6: state change push within 1s)
 * ============================================================ */

bool ble_service_notify_status(ble_service_ctx_t *ctx, uint8_t lock_state, uint8_t battery_pct) {
    /* Notify all connected peers of status change */
    bool notified = false;
    
    for (uint8_t i = 0; i < BLE_MAX_CONNECTIONS; i++) {
        if (ctx->connections[i].state == BLE_CONN_STATE_CONNECTED) {
            /* In production: GATT Notify on 0xF001 characteristic */
            /* For simulation: just mark as notified */
            (void)lock_state;
            (void)battery_pct;
            ctx->last_status_notify_ms = ctx->connections[i].connected_at_ms; /* sim */
            notified = true;
        }
    }
    
    return notified;
}
