/*
 * main.c — BLE Smart Lock Firmware Entry Point
 *
 * T-2026-00052: Bug fixes + Low battery shutdown
 */

#include "lock_engine.h"
#include "power_manager.h"
#include <stdio.h>
#include <stdint.h>

/* FreeRTOS-style task simulation (for testing) */
static lock_engine_ctx_t g_lock_engine;
static power_manager_ctx_t g_power_manager;

/* Simulated hardware functions */
static void motor_driver_start(bool unlock) {
    (void)unlock;
    /* GPIO control for motor driver */
}

static void motor_driver_stop(void) {
    /* GPIO low to stop motor */
}

static void beeper_start(bool on) {
    (void)on;
    /* GPIO control for beeper */
}

static int32_t battery_read_voltage_mv(void) {
    /* INA219 ADC read — in test, this is mocked */
    return 4200;
}

static void system_enter_shutdown(void) {
    /* nRF52 System OFF — only RESET button or NFC can wake */
}

/* Task: Power monitoring (100ms interval) */
static void power_monitor_task(void) {
    uint32_t now_ms = 0; /* simulated time */

    while (1) {
        int32_t voltage = battery_read_voltage_mv();
        power_manager_update(&g_power_manager, voltage, now_ms);

        if (g_power_manager.shutdown_requested) {
            lock_engine_transition(&g_lock_engine, LOCK_STATE_SHUTDOWN);
            system_enter_shutdown();
            break;
        }

        /* Update beeper */
        power_manager_beeper_update(&g_power_manager, now_ms);

        /* Check for low battery → inject event into lock engine */
        if (g_power_manager.state == POWER_STATE_LOW_BATTERY) {
            g_lock_engine.low_battery = true;
            lock_event_t_struct evt = {
                .type = EVT_LOW_BATTERY,
                .timestamp_ms = now_ms,
                .data_len = 0,
            };
            lock_engine_push_event(&g_lock_engine, &evt);
        }

        now_ms += 100;
    }
}

/* Task: Lock engine (10ms interval) */
static void lock_engine_task(void) {
    uint32_t now_ms = 0;

    lock_engine_init(&g_lock_engine);
    power_manager_init(&g_power_manager);

    while (g_lock_engine.state != LOCK_STATE_SHUTDOWN) {
        lock_engine_run(&g_lock_engine, now_ms);
        now_ms += 10;
    }
}

int main(void) {
    lock_engine_init(&g_lock_engine);
    power_manager_init(&g_power_manager);

    /* In production: FreeRTOS task creation */
    // xTaskCreate(power_monitor_task, "PowerMon", 512, NULL, 3, NULL);
    // xTaskCreate(lock_engine_task, "LockEngine", 1024, NULL, 2, NULL);

    /* Simulation loop */
    lock_engine_task();

    return 0;
}
