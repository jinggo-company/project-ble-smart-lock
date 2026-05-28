#ifndef ANTI_REPLAY_H
#define ANTI_REPLAY_H

#include <stdint.h>
#include <stdbool.h>

/* ============================================================
 * Anti-Replay Module — Nonce + Timestamp window check
 *
 * T-2026-00053: App对接联调 + AC-6 数据通信 (防重放)
 * ============================================================ */

#define ANTI_REPLAY_TIMESTAMP_WINDOW_MS  5000  /* ±5s */
#define ANTI_REPLAY_NONCE_HISTORY_SIZE   64

typedef struct {
    uint32_t last_nonce;
    uint32_t nonce_history[ANTI_REPLAY_NONCE_HISTORY_SIZE];
    uint8_t  nonce_history_count;
    uint8_t  nonce_history_head;
} anti_replay_ctx_t;

void anti_replay_init(anti_replay_ctx_t *ctx);
bool anti_replay_check_nonce(anti_replay_ctx_t *ctx, uint32_t nonce);
bool anti_replay_check_timestamp(uint32_t msg_timestamp_ms, uint32_t current_ms);

#endif /* ANTI_REPLAY_H */
