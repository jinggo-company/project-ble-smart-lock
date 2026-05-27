# BLE Smart Lock Firmware

## Project Info
- **Project ID:** P-2026-00011
- **Tasks:** T-2026-00044 (Architecture), T-2026-00052 (Bug Fixes + Low Power)
- **Platform:** nRF52832/52840 SoC (Nordic Semiconductor)
- **RTOS:** FreeRTOS + Nordic SoftDevice S132/S140

## Firmware Directory Structure

```
firmware/
├── src/
│   ├── main.c                  # Entry point, FreeRTOS task creation
│   ├── ble/
│   │   ├── ble_service.c       # GATT service definition
│   │   ├── ble_handler.c       # Connection/pairing event handling
│   │   ├── ble_crypto.c        # AES-128 encryption
│   │   └── ble_ota.c           # OTA DFU handling
│   ├── lock/
│   │   ├── lock_engine.c       # Lock state machine (core logic)
│   │   ├── lock_motor.c        # Motor driver (GPIO control)
│   │   ├── lock_sensor.c       # Hall sensor (latch detection)
│   │   └── lock_log.c          # Lock log (Flash ring buffer)
│   ├── power/
│   │   ├── power_manager.c     # Low-power management + battery monitor
│   └── crypto/
│       ├── key_store.c         # Secure key storage
│       ├── hmac_verify.c       # HMAC-SHA256 verification
│       └── anti_replay.c       # Anti-replay (Nonce + timestamp)
├── include/
│   ├── lock_engine.h           # State machine public API
│   └── power_manager.h         # Power manager public API
└── tests/
    └── test_lock_engine.c      # Unit test suite
```

## Build Instructions

### For Development (Host Testing)

```bash
# Compile and run unit tests
gcc -std=c99 -Wall -Wextra -o tests/test_runner \
    tests/test_lock_engine.c \
    firmware/src/lock/lock_engine.c \
    firmware/src/power/power_manager.c \
    -I firmware/include

# Run tests
./tests/test_runner
```

### For nRF52 Target (Production)

```bash
# Requires nRF Connect SDK + ARM GCC toolchain
cd firmware
mkdir build && cd build
cmake -GNinja -DBOARD=nrf52840dk_nrf52840 ..
ninja
nrfjprog --program build/zephyr/zephyr.hex --chiperase --reset
```

## This Task (T-2026-00052)

### Bug Fixes
1. **Bug1 — 杆多次插入后锁无响应**: Bounded event queue with overflow protection. Drops oldest events when full, preventing state machine deadlock.
2. **Bug2 — 解锁状态超时后无法上锁**: Timeout handler now properly transitions from `LOCK_STATE_UNLOCKED` to `LOCK_STATE_STANDBY`.
3. **Bug3 — 上锁状态解锁后状态残留**: `prev_state` tracking + explicit state validation after every event.

### New Feature
- **Low Battery Shutdown (AC-4)**: Voltage ≤ 3.0V → 3s countdown → beeper alert → shutdown → reject all commands.

## Test Results

All 11 tests pass (see `docs/TEST_REPORT.md` for details).

```
Results: 11 passed, 0 failed, 11 total
```
