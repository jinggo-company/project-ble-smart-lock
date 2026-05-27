/*
 * power_manager.c — Battery Monitor + Low Battery Shutdown
 *
 * Features (AC-4):
 *   - Voltage monitoring with hysteresis
 *   - Low battery detection at ≤ 3.0V
 *   - 3-second shutdown delay after detection
 *   - Beeper alert (1Hz, 3s duration)
 *   - Rejects all lock/unlock commands when low
 */

#include "power_manager.h"

void power_manager_init(power_manager_ctx_t *ctx) {
    ctx->state = POWER_STATE_NORMAL;
    ctx->voltage_mv = 4200; /* fresh battery */
    ctx->battery_pct = 100;
    ctx->low_battery_start_ms = 0;
    ctx->beeper_active = false;
    ctx->beeper_start_ms = 0;
    ctx->shutdown_requested = false;
}

static uint16_t voltage_to_percentage(int32_t voltage_mv) {
    /* Simple linear mapping: 3.0V = 0%, 4.2V = 100% */
    if (voltage_mv >= 4200) return 100;
    if (voltage_mv <= 3000) return 0;
    return (uint16_t)(((voltage_mv - 3000) * 100) / 1200);
}

void power_manager_update(power_manager_ctx_t *ctx, int32_t voltage_mv, uint32_t now_ms) {
    ctx->voltage_mv = voltage_mv;
    ctx->battery_pct = voltage_to_percentage(voltage_mv);

    switch (ctx->state) {
        case POWER_STATE_NORMAL:
            if (voltage_mv <= POWER_LOW_BATTERY_THRESHOLD_MV) {
                ctx->state = POWER_STATE_LOW_BATTERY;
                ctx->low_battery_start_ms = now_ms;
                ctx->beeper_active = true;
                ctx->beeper_start_ms = now_ms;
            } else if (voltage_mv <= POWER_WARNING_THRESHOLD_MV) {
                ctx->state = POWER_STATE_WARNING;
            }
            break;

        case POWER_STATE_WARNING:
            if (voltage_mv <= POWER_LOW_BATTERY_THRESHOLD_MV) {
                ctx->state = POWER_STATE_LOW_BATTERY;
                ctx->low_battery_start_ms = now_ms;
                ctx->beeper_active = true;
                ctx->beeper_start_ms = now_ms;
            } else if (voltage_mv >= POWER_NORMAL_THRESHOLD_MV) {
                ctx->state = POWER_STATE_NORMAL;
            }
            break;

        case POWER_STATE_LOW_BATTERY:
            if (voltage_mv >= POWER_NORMAL_THRESHOLD_MV) {
                /* Recovered — battery charged */
                ctx->state = POWER_STATE_NORMAL;
                ctx->beeper_active = false;
            } else {
                /* Check if shutdown delay has elapsed */
                uint32_t elapsed = now_ms - ctx->low_battery_start_ms;
                if (elapsed >= POWER_SHUTDOWN_DELAY_MS) {
                    ctx->shutdown_requested = true;
                    ctx->state = POWER_STATE_SHUTDOWN;
                    ctx->beeper_active = false;
                }
            }
            break;

        case POWER_STATE_SHUTDOWN:
            /* No recovery — only hardware reset */
            break;
    }
}

power_state_t power_manager_get_state(const power_manager_ctx_t *ctx) {
    return ctx->state;
}

bool power_manager_is_shutdown_imminent(const power_manager_ctx_t *ctx) {
    return ctx->state == POWER_STATE_LOW_BATTERY && ctx->shutdown_requested;
}

void power_manager_trigger_shutdown(power_manager_ctx_t *ctx) {
    ctx->shutdown_requested = true;
    ctx->state = POWER_STATE_SHUTDOWN;
    ctx->beeper_active = false;
}

/* Beeper: 1Hz on/off cycle, active for 3s total */
void power_manager_beeper_update(power_manager_ctx_t *ctx, uint32_t now_ms) {
    if (!ctx->beeper_active) return;

    uint32_t elapsed = now_ms - ctx->beeper_start_ms;
    if (elapsed >= BEEPER_DURATION_MS) {
        ctx->beeper_active = false;
        return;
    }

    /* 1Hz toggle: on for 500ms, off for 500ms */
    uint32_t cycle_pos = elapsed % BEEPER_PERIOD_MS;
    ctx->beeper_active = (cycle_pos < BEEPER_PERIOD_MS / 2);
}

bool power_manager_beeper_is_on(const power_manager_ctx_t *ctx, uint32_t now_ms) {
    if (!ctx->beeper_active) return false;
    uint32_t elapsed = now_ms - ctx->beeper_start_ms;
    if (elapsed >= BEEPER_DURATION_MS) return false;
    uint32_t cycle_pos = elapsed % BEEPER_PERIOD_MS;
    return (cycle_pos < BEEPER_PERIOD_MS / 2);
}
