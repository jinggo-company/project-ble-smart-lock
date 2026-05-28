#ifndef BLE_OTA_H
#define BLE_OTA_H

#include <stdint.h>
#include <stdbool.h>

/* ============================================================
 * BLE OTA DFU Module — Nordic Secure DFU
 *
 * T-2026-00053: 固件版本管理
 * ============================================================ */

#define OTA_FIRMWARE_VERSION_MAJOR  1
#define OTA_FIRMWARE_VERSION_MINOR  2
#define OTA_FIRMWARE_VERSION_PATCH  0
#define OTA_FIRMWARE_VERSION_STRING "1.2.0"

#define OTA_MAX_PACKET_SIZE     247  /* BLE MTU max */
#define OTA_PACKET_TIMEOUT_MS   5000 /* per-packet timeout */
#define OTA_MIN_BATTERY_PCT     30   /* refuse OTA below 30% battery */

/* OTA states */
typedef enum {
    OTA_STATE_IDLE,
    OTA_STATE_INIT,
    OTA_STATE_RECEIVING,
    OTA_STATE_VALIDATING,
    OTA_STATE_COMPLETE,
    OTA_STATE_FAILED,
} ota_state_t;

/* Firmware version info */
typedef struct {
    uint8_t major;
    uint8_t minor;
    uint8_t patch;
    uint32_t build_number;
    char version_string[16];
} firmware_version_t;

/* OTA context */
typedef struct {
    ota_state_t state;
    firmware_version_t current_version;
    firmware_version_t pending_version;
    uint32_t total_size;
    uint32_t received_bytes;
    uint32_t last_packet_time_ms;
    uint8_t  last_packet_crc;
    bool     signature_valid;
} ota_ctx_t;

/* API */
void ota_init(ota_ctx_t *ctx);
void ota_set_current_version(ota_ctx_t *ctx, uint8_t major, uint8_t minor, uint8_t patch);
firmware_version_t ota_get_current_version(const ota_ctx_t *ctx);
bool ota_can_start(const ota_ctx_t *ctx, uint8_t battery_pct);
bool ota_start(ota_ctx_t *ctx, const firmware_version_t *new_version, uint32_t total_size);
bool ota_receive_packet(ota_ctx_t *ctx, const uint8_t *data, uint16_t len, uint32_t now_ms);
bool ota_validate(ota_ctx_t *ctx);
bool ota_complete(ota_ctx_t *ctx);
uint32_t ota_progress(const ota_ctx_t *ctx);

#endif /* BLE_OTA_H */
