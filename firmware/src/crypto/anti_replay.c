/*
 * anti_replay.c — Anti-Replay Protection (Nonce + Timestamp)
 *
 * T-2026-00053: App对接联调 + AC-6 数据通信
 */

#include "anti_replay.h"
#include <string.h>

void anti_replay_init(anti_replay_ctx_t *ctx) {
    memset(ctx, 0, sizeof(*ctx));
}

bool anti_replay_check_nonce(anti_replay_ctx_t *ctx, uint32_t nonce) {
    if (!ctx) return false;
    
    /* Must be strictly increasing from last accepted nonce */
    if (nonce <= ctx->last_nonce) {
        return false; /* replay or out of order */
    }
    
    /* Check history for duplicates */
    for (uint8_t i = 0; i < ctx->nonce_history_count; i++) {
        if (ctx->nonce_history[i] == nonce) {
            return false; /* duplicate in history */
        }
    }
    
    /* Accept and store in history */
    ctx->last_nonce = nonce;
    uint8_t idx = ctx->nonce_history_head;
    ctx->nonce_history[idx] = nonce;
    ctx->nonce_history_head = (idx + 1) % ANTI_REPLAY_NONCE_HISTORY_SIZE;
    if (ctx->nonce_history_count < ANTI_REPLAY_NONCE_HISTORY_SIZE) {
        ctx->nonce_history_count++;
    }
    
    return true;
}

bool anti_replay_check_timestamp(uint32_t msg_timestamp_ms, uint32_t current_ms) {
    /* Accept if within ±5s window */
    int64_t diff = (int64_t)msg_timestamp_ms - (int64_t)current_ms;
    if (diff < 0) diff = -diff;
    
    return (uint64_t)diff <= ANTI_REPLAY_TIMESTAMP_WINDOW_MS;
}
