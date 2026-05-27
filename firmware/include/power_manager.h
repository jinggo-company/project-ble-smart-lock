#ifndef POWER_MANAGER_H
#define POWER_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include "lock_engine.h"

/* ============================================================
 * Power Manager — Low Battery Shutdown (T-2026-00052)
 *
 * Features:
 *   - Battery voltage monitoring via INA219 ADC
 *   - Low battery threshold: 3.0V → shutdown sequence
 *   - Beeper alert at low voltage (1Hz, 3s duration)
 *   - Power state management for nRF52
 * ============================================================ */

/* Voltage thresholds (mV) */
#define POWER_LOW_BATTERY_THRESHOLD_MV    3000   /* ≤ 3.0V → shutdown */
#define POWER_WARNING_THRESHOLD_MV        3300   /* ≤ 3.3V → warning */
#define POWER_NORMAL_THRESHOLD_MV         3600   /* ≥ 3.6V → normal */
#define POWER_SHUTDOWN_DELAY_MS           3000   /* 3s delay before shutdown */

/* Beeper control */
#define BEEPER_LOW_FREQ_HZ                1      /* 1Hz beep */
#define BEEPER_DURATION_MS                3000   /* 3s duration */
#define BEEPER_PERIOD_MS                  1000   /* 1Hz period */

typedef enum {
    POWER_STATE_NORMAL = 0,
    POWER_STATE_WARNING,
    POWER_STATE_LOW_BATTERY,
    POWER_STATE_SHUTDOWN,
} power_state_t;

typedef struct {
    power_state_t state;
    int32_t voltage_mv;
    uint16_t battery_pct;
    uint32_t low_battery_start_ms;  /* timestamp when low battery first detected */
    bool beeper_active;
    uint32_t beeper_start_ms;
    bool shutdown_requested;
} power_manager_ctx_t;

/* API */
void power_manager_init(power_manager_ctx_t *ctx);
void power_manager_update(power_manager_ctx_t *ctx, int32_t voltage_mv, uint32_t now_ms);
power_state_t power_manager_get_state(const power_manager_ctx_t *ctx);
bool power_manager_is_shutdown_imminent(const power_manager_ctx_t *ctx);
void power_manager_trigger_shutdown(power_manager_ctx_t *ctx);

/* Beeper control */
void power_manager_beeper_update(power_manager_ctx_t *ctx, uint32_t now_ms);
bool power_manager_beeper_is_on(const power_manager_ctx_t *ctx, uint32_t now_ms);

#endif /* POWER_MANAGER_H */
