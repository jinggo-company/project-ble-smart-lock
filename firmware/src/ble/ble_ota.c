/*
 * ble_ota.c — OTA DFU Firmware Version Management
 *
 * T-2026-00053: 固件版本管理 (AC-7)
 * Implements: firmware version reporting, OTA upgrade flow
 */

#include "ble_ota.h"
#include <string.h>
#include <stdio.h>

void ota_init(ota_ctx_t *ctx) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->state = OTA_STATE_IDLE;
    ctx->current_version.major = OTA_FIRMWARE_VERSION_MAJOR;
    ctx->current_version.minor = OTA_FIRMWARE_VERSION_MINOR;
    ctx->current_version.patch = OTA_FIRMWARE_VERSION_PATCH;
    ctx->current_version.build_number = 1;
    snprintf(ctx->current_version.version_string, sizeof(ctx->current_version.version_string),
             "%d.%d.%d", OTA_FIRMWARE_VERSION_MAJOR, OTA_FIRMWARE_VERSION_MINOR, OTA_FIRMWARE_VERSION_PATCH);
}

void ota_set_current_version(ota_ctx_t *ctx, uint8_t major, uint8_t minor, uint8_t patch) {
    ctx->current_version.major = major;
    ctx->current_version.minor = minor;
    ctx->current_version.patch = patch;
    ctx->current_version.build_number++;
    snprintf(ctx->current_version.version_string, sizeof(ctx->current_version.version_string),
             "%d.%d.%d", major, minor, patch);
}

firmware_version_t ota_get_current_version(const ota_ctx_t *ctx) {
    return ctx->current_version;
}

bool ota_can_start(const ota_ctx_t *ctx, uint8_t battery_pct) {
    /* Refuse OTA if battery too low */
    if (battery_pct < OTA_MIN_BATTERY_PCT) {
        return false;
    }
    return ctx->state == OTA_STATE_IDLE;
}

bool ota_start(ota_ctx_t *ctx, const firmware_version_t *new_version, uint32_t total_size) {
    if (!ctx || !new_version) return false;
    
    /* Prevent downgrade (unless force mode — not implemented here) */
    if (new_version->major < ctx->current_version.major) {
        return false;
    }
    if (new_version->major == ctx->current_version.major &&
        new_version->minor < ctx->current_version.minor) {
        return false;
    }
    if (new_version->major == ctx->current_version.major &&
        new_version->minor == ctx->current_version.minor &&
        new_version->patch <= ctx->current_version.patch) {
        return false;
    }
    
    memcpy(&ctx->pending_version, new_version, sizeof(*new_version));
    ctx->total_size = total_size;
    ctx->received_bytes = 0;
    ctx->state = OTA_STATE_INIT;
    
    return true;
}

bool ota_receive_packet(ota_ctx_t *ctx, const uint8_t *data, uint16_t len, uint32_t now_ms) {
    if (!ctx || !data) return false;
    
    if (ctx->state != OTA_STATE_INIT && ctx->state != OTA_STATE_RECEIVING) {
        return false;
    }
    
    /* Check packet timeout */
    if (ctx->state == OTA_STATE_RECEIVING) {
        if (now_ms - ctx->last_packet_time_ms > OTA_PACKET_TIMEOUT_MS) {
            ctx->state = OTA_STATE_FAILED;
            return false;
        }
    }
    
    /* Simple CRC check on packet */
    uint8_t crc = 0;
    for (uint16_t i = 0; i < len; i++) {
        crc ^= data[i];
    }
    ctx->last_packet_crc = crc;
    
    ctx->received_bytes += len;
    ctx->last_packet_time_ms = now_ms;
    ctx->state = OTA_STATE_RECEIVING;
    
    /* Check if all data received */
    if (ctx->received_bytes >= ctx->total_size) {
        ctx->state = OTA_STATE_VALIDATING;
    }
    
    return true;
}

bool ota_validate(ota_ctx_t *ctx) {
    if (ctx->state != OTA_STATE_VALIDATING) return false;
    
    /* In production: ECDSA P-256 signature verification of firmware */
    /* For simulation: accept if all bytes received */
    if (ctx->received_bytes >= ctx->total_size) {
        ctx->signature_valid = true;
        return true;
    }
    
    ctx->state = OTA_STATE_FAILED;
    return false;
}

bool ota_complete(ota_ctx_t *ctx) {
    if (ctx->state != OTA_STATE_VALIDATING) return false;
    if (!ctx->signature_valid) return false;
    
    /* Accept new version */
    memcpy(&ctx->current_version, &ctx->pending_version, sizeof(ctx->current_version));
    ctx->state = OTA_STATE_COMPLETE;
    
    return true;
}

uint32_t ota_progress(const ota_ctx_t *ctx) {
    if (ctx->total_size == 0) return 0;
    return (ctx->received_bytes * 100) / ctx->total_size;
}
