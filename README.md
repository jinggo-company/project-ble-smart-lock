# BLE Smart Lock Firmware

## Project Info
- **Project ID:** P-2026-00011
- **Tasks:** T-2026-00044 (Architecture), T-2026-00052 (Bug Fixes + Low Power), T-2026-00053 (App Integration + Firmware Version Management)
- **Platform:** nRF52832/52840 SoC (Nordic Semiconductor)
- **RTOS:** FreeRTOS + Nordic SoftDevice S132/S140

## Firmware Directory Structure

```
firmware/
├── src/
│   ├── main.c                  # Entry point, FreeRTOS task creation
│   ├── ble/
│   │   ├── ble_service.c       # GATT service definition (T-2026-00053)
│   │   ├── ble_handler.c       # Connection/pairing event handling
│   │   ├── ble_crypto.c        # AES-128 encryption (T-2026-00053)
│   │   └── ble_ota.c           # OTA DFU handling (T-2026-00053)
│   ├── lock/
│   │   ├── lock_engine.c       # Lock state machine (core logic)
│   │   ├── lock_motor.c        # Motor driver (GPIO control)
│   │   ├── lock_sensor.c       # Hall sensor (latch detection)
│   │   └── lock_log.c          # Lock log (Flash ring buffer)
│   ├── power/
│   │   ├── power_manager.c     # Low-power management + battery monitor
│   └── crypto/
│       ├── key_store.c         # Secure key storage
│       ├── hmac_verify.c       # HMAC-SHA256 verification (T-2026-00053)
│       └── anti_replay.c       # Anti-replay (Nonce + timestamp) (T-2026-00053)
├── include/
│   ├── lock_engine.h           # State machine public API
│   ├── power_manager.h         # Power manager public API
│   ├── ble_service.h           # BLE GATT service (T-2026-00053)
│   ├── ble_crypto.h            # BLE crypto module (T-2026-00053)
│   ├── ble_ota.h               # OTA/Version management (T-2026-00053)
│   ├── hmac_verify.h           # HMAC verification (T-2026-00053)
│   └── anti_replay.h           # Anti-replay protection (T-2026-00053)
├── tests/
│   ├── test_lock_engine.c      # Unit test suite (T-2026-00052)
│   └── test_ble_app_integration.c # BLE app integration tests (T-2026-00053)
└── app/
    └── src/
        ├── services/
        │   ├── bleManager.ts   # BLE connection management (T-2026-00053)
        │   └── lockService.ts  # Lock command service (T-2026-00053)
        └── crypto/
            ├── aes.ts          # AES-128 crypto wrapper (T-2026-00053)
            └── hmac.ts         # HMAC-SHA256 wrapper (T-2026-00053)
```

## Build Instructions

### For Development (Host Testing)

```bash
# Compile and run unit tests (T-2026-00052: Bug Fixes + Low Power)
gcc -std=c99 -Wall -Wextra -o tests/test_runner \
    tests/test_lock_engine.c \
    firmware/src/lock/lock_engine.c \
    firmware/src/power/power_manager.c \
    -I firmware/include

./tests/test_runner

# Compile and run BLE app integration tests (T-2026-00053)
gcc -std=c99 -Wall -Wextra -Wno-unused-function \
    -o tests/test_ble_app_integration \
    tests/test_ble_app_integration.c

./tests/test_ble_app_integration
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

## Tasks Completed

### T-2026-00052: Bug Fixes + Low Battery Shutdown
1. **Bug1 — Rod repeated insertion causes unresponsiveness**: Bounded event queue with overflow protection.
2. **Bug2 — Cannot lock after unlock timeout**: Timeout handler transitions to STANDBY.
3. **Bug3 — State residue after unlock**: `prev_state` tracking + explicit state validation.
4. **Low Battery Shutdown**: Voltage ≤ 3.0V → 3s countdown → beeper → shutdown.

All 11 tests pass.

### T-2026-00053: App Integration + Firmware Version Management
1. **AC-5 — BLE Connection**: GATT service discovery (≤5s), max 3 connections, auto-reconnect.
2. **AC-6 — Data Communication**: AES-128 encryption, HMAC integrity, anti-replay (nonce + timestamp), status notify.
3. **AC-7 — Firmware Version Management**: Version 1.2.0, OTA upgrade flow, downgrade prevention.

All 19 tests pass.

## Test Results

```
T-2026-00052: 11 passed, 0 failed
T-2026-00053: 19 passed, 0 failed
Total:        30 passed, 0 failed
```
