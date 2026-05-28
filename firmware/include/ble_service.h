#ifndef BLE_SERVICE_H
#define BLE_SERVICE_H

#include <stdint.h>
#include <stdbool.h>

/* ============================================================
 * BLE GATT Service — Lock Control Service
 * 
 * Service UUID: 0x0000FE15 (custom)
 * Characteristics:
 *   0xF001 — Lock Status (Notify, 2B)
 *   0xF002 — Lock Command (Write + Auth)
 *   0xF003 — Lock Log (Read + Notify)
 *   0xF004 — Key Management (Write + Auth)
 *   0xF005 — OTA Control (Write)
 *   0xF006 — OTA Data (Write)
 * ============================================================ */

#define BLE_LOCK_SERVICE_UUID        0xFE15
#define BLE_CHAR_LOCK_STATUS_UUID    0xF001
#define BLE_CHAR_LOCK_CMD_UUID       0xF002
#define BLE_CHAR_LOCK_LOG_UUID       0xF003
#define BLE_CHAR_KEY_MGMT_UUID       0xF004
#define BLE_CHAR_OTA_CTRL_UUID       0xF005
#define BLE_CHAR_OTA_DATA_UUID       0xF006

/* Max concurrent connections */
#define BLE_MAX_CONNECTIONS          3

/* Connection timeout (ms) */
#define BLE_CONNECTION_TIMEOUT_MS    30000

/* GATT service discovery timeout (AC-5: must complete within 5s) */
#define BLE_DISCOVERY_TIMEOUT_MS     5000

/* Auto-reconnect settings (AC-5: reconnect within 10s, 3 retries) */
#define BLE_RECONNECT_MAX_RETRIES    3
#define BLE_RECONNECT_BASE_DELAY_MS  1000  /* exponential backoff base */
#define BLE_RECONNECT_WINDOW_MS      10000 /* must reconnect within this window */

/* Message frame constants (see ARCHITECTURE.md §5.1) */
#define MSG_MAGIC_1       0xAA
#define MSG_MAGIC_2       0x55
#define MSG_FOOTER_1      0x0D
#define MSG_FOOTER_2      0x0A
#define MSG_NONCE_SIZE    4
#define MSG_CMD_SIZE      1
#define MSG_HMAC_SIZE     4
#define MSG_HEADER_SIZE   2
#define MSG_FOOTER_SIZE   2
#define MSG_MIN_FRAME_SIZE (MSG_HEADER_SIZE + MSG_NONCE_SIZE + MSG_CMD_SIZE + MSG_HMAC_SIZE + MSG_FOOTER_SIZE)

/* Command codes */
#define BLE_CMD_UNLOCK      0x01
#define BLE_CMD_LOCK        0x02
#define BLE_CMD_QUERY       0x03
#define BLE_CMD_READ_LOG    0x04
#define BLE_CMD_KEY_MGMT    0x05
#define BLE_CMD_OTA         0x06

/* Result codes returned to App */
#define BLE_RESULT_OK           0x00
#define BLE_RESULT_FAIL         0x01
#define BLE_RESULT_AUTH_FAIL    0x03
#define BLE_RESULT_REJECTED     0x09  /* unauthorized/encrypted */
#define BLE_RESULT_LOW_BATTERY  0x02

/* Connection state */
typedef enum {
    BLE_CONN_STATE_IDLE,
    BLE_CONN_STATE_CONNECTING,
    BLE_CONN_STATE_CONNECTED,
    BLE_CONN_STATE_DISCONNECTED,
} ble_conn_state_t;

/* BLE connection context */
typedef struct {
    uint8_t conn_id;                    /* connection handle */
    ble_conn_state_t state;
    uint32_t connected_at_ms;
    bool paired;
    bool encrypted;
    uint8_t peer_addr[6];               /* BLE MAC address */
    uint32_t nonce_tx;                  /* monotonic send counter */
    uint32_t nonce_rx;                  /* monotonic recv counter */
} ble_connection_t;

/* BLE service context */
typedef struct {
    ble_connection_t connections[BLE_MAX_CONNECTIONS];
    uint8_t active_connections;
    uint32_t service_discovery_start_ms;
    bool service_discovery_done;
    uint32_t last_status_notify_ms;
    uint8_t session_key[16];            /* AES-128 session key */
    bool session_key_valid;
} ble_service_ctx_t;

/* Message frame */
typedef struct {
    uint8_t header[2];
    uint8_t nonce[MSG_NONCE_SIZE];
    uint8_t cmd;
    uint8_t payload[240];              /* variable, max ~240 for BLE MTU */
    uint16_t payload_len;
    uint8_t hmac[MSG_HMAC_SIZE];
    uint8_t footer[2];
    uint16_t total_len;
} ble_msg_frame_t;

/* API */
void ble_service_init(ble_service_ctx_t *ctx);
int8_t ble_service_accept_connection(ble_service_ctx_t *ctx, const uint8_t peer_addr[6]);
void ble_service_disconnect(ble_service_ctx_t *ctx, uint8_t conn_id);
bool ble_service_validate_frame(ble_service_ctx_t *ctx, const ble_msg_frame_t *frame);
bool ble_service_build_response(ble_service_ctx_t *ctx, uint8_t cmd, uint8_t result, uint8_t err_code, ble_msg_frame_t *out);
bool ble_service_encrypt_frame(ble_service_ctx_t *ctx, ble_msg_frame_t *frame);
bool ble_service_decrypt_frame(ble_service_ctx_t *ctx, ble_msg_frame_t *frame);
bool ble_service_notify_status(ble_service_ctx_t *ctx, uint8_t lock_state, uint8_t battery_pct);
void ble_service_on_connect(ble_service_ctx_t *ctx, uint8_t conn_id, const uint8_t peer_addr[6], uint32_t now_ms);
void ble_service_on_disconnect(ble_service_ctx_t *ctx, uint8_t conn_id, uint32_t now_ms);
void ble_service_start_discovery(ble_service_ctx_t *ctx, uint32_t now_ms);
bool ble_service_is_discovery_complete(const ble_service_ctx_t *ctx, uint32_t now_ms);
uint8_t ble_service_active_connection_count(const ble_service_ctx_t *ctx);
void ble_service_increment_tx_nonce(ble_service_ctx_t *ctx);
uint32_t ble_service_get_tx_nonce(const ble_service_ctx_t *ctx);

#endif /* BLE_SERVICE_H */
