#ifndef LOCK_ENGINE_H
#define LOCK_ENGINE_H

#include <stdint.h>
#include <stdbool.h>

/* ============================================================
 * Lock State Machine — Fixed Version (T-2026-00052)
 * 
 * Bug fixes applied:
 *   Bug1: Event queue deadlock on repeated rod insertions
 *   Bug2: Cannot lock after unlock timeout
 *   Bug3: State residue after unlock→timeout→rod removal
 * ============================================================ */

/* Lock states — explicit enum, no implicit states */
typedef enum {
    LOCK_STATE_INIT = 0,
    LOCK_STATE_STANDBY,
    LOCK_STATE_PAIRING,
    LOCK_STATE_CONNECTED,
    LOCK_STATE_UNLOCKING,
    LOCK_STATE_UNLOCKED,        /* unlocked, waiting for auto-lock */
    LOCK_STATE_LOCKED,
    LOCK_STATE_LOCKED_FAIL,
    LOCK_STATE_ALARM,
    LOCK_STATE_LOW_POWER,       /* Bug4: new low-power state */
    LOCK_STATE_ERROR,
    LOCK_STATE_SHUTDOWN,        /* Bug4: fully shutdown */
    LOCK_STATE_MAX
} lock_state_t;

/* Lock commands */
typedef enum {
    CMD_UNLOCK = 0x01,
    CMD_LOCK = 0x02,
    CMD_QUERY_STATUS = 0x03,
    CMD_READ_LOG = 0x04,
    CMD_KEY_MGMT = 0x05,
    CMD_OTA = 0x06,
} lock_cmd_t;

/* Result codes */
typedef enum {
    RESULT_OK = 0x00,
    RESULT_FAIL = 0x01,
    RESULT_LOW_BATTERY = 0x02,
    RESULT_AUTH_FAIL = 0x03,
    RESULT_ALREADY_LOCKED = 0x04,
    RESULT_ALREADY_UNLOCKED = 0x05,
    RESULT_TIMEOUT = 0x06,
    RESULT_ALARM = 0x07,
    RESULT_SHUTDOWN = 0x08,
} lock_result_t;

/* Error codes */
typedef enum {
    ERR_NONE = 0x00,
    ERR_MOTOR_STALL = 0x01,
    ERR_SENSOR_FAIL = 0x02,
    ERR_QUEUE_OVERFLOW = 0x03,   /* Bug1: queue overflow detection */
    ERR_STATE_INVALID = 0x04,
    ERR_WATCHDOG = 0x05,
} lock_error_t;

/* Event types for the lock event queue */
typedef enum {
    EVT_NONE = 0,
    EVT_UNLOCK_CMD,
    EVT_LOCK_CMD,
    EVT_TIMEOUT,               /* unlock timeout expired */
    EVT_ROD_INSERTED,          /* rod/key inserted */
    EVT_ROD_REMOVED,           /* rod/key removed */
    EVT_MOTOR_DONE,            /* motor completed */
    EVT_MOTOR_STALL,           /* motor stalled */
    EVT_SENSOR_TRIGGER,        /* Hall sensor triggered */
    EVT_LOW_BATTERY,           /* voltage dropped below threshold */
    EVT_ALARM_TIMEOUT,         /* alarm period expired */
    EVT_BLE_CONNECT,
    EVT_BLE_DISCONNECT,
    EVT_OTA_REQUEST,
    EVT_MAX
} lock_event_t;

/* Event structure */
typedef struct {
    lock_event_t type;
    uint32_t timestamp_ms;
    uint8_t data[4];
    uint8_t data_len;
} lock_event_t_struct;

/* Event queue — Bug1 fix: bounded queue with overflow protection */
#define LOCK_EVENT_QUEUE_SIZE   16

typedef struct {
    lock_event_t_struct events[LOCK_EVENT_QUEUE_SIZE];
    uint8_t head;
    uint8_t tail;
    uint8_t count;
    uint32_t overflow_count;     /* track overflows for diagnostics */
} lock_event_queue_t;

/* Lock engine context */
typedef struct {
    lock_state_t state;
    lock_state_t prev_state;     /* for Bug3: proper state tracking */
    lock_error_t error;
    uint32_t state_entry_time_ms;
    uint32_t unlock_timeout_ms;  /* auto-lock timeout */
    uint8_t fail_count;          /* consecutive auth failures */
    uint8_t unlock_attempt_count;/* rod insertion counter (Bug1 tracking) */
    bool low_battery;
    bool motor_running;
    bool sensor_ok;
    uint16_t battery_pct;
    int32_t battery_voltage_mv;
    lock_event_queue_t event_queue;
} lock_engine_ctx_t;

/* API */
void lock_engine_init(lock_engine_ctx_t *ctx);
void lock_engine_reset_state(lock_engine_ctx_t *ctx);  /* Bug3 fix helper */
void lock_engine_run(lock_engine_ctx_t *ctx, uint32_t now_ms);
bool lock_engine_push_event(lock_engine_ctx_t *ctx, const lock_event_t_struct *evt);
lock_state_t lock_engine_get_state(const lock_engine_ctx_t *ctx);
lock_result_t lock_engine_get_last_result(const lock_engine_ctx_t *ctx);

/* Internal (public for testing) */
bool lock_engine_can_unlock(const lock_engine_ctx_t *ctx);
bool lock_engine_can_lock(const lock_engine_ctx_t *ctx);
void lock_engine_transition(lock_engine_ctx_t *ctx, lock_state_t new_state);

#endif /* LOCK_ENGINE_H */
