# TECH_STACK — BLE 智能锁固件开发与 App 对接

## 项目信息

| 字段 | 值 |
|------|------|
| 项目 ID | P-2026-00011 |
| 关联任务 | T-2026-00044 |
| 工期 | 14 天 |

---

## 1. 固件端（锁控板）

| 组件 | 选型 | 版本 | 说明 |
|------|------|------|------|
| MCU | Nordic nRF52832 / nRF52840 | — | BLE 5.0 SoC，超低功耗，内置加密硬件 |
| RTOS | FreeRTOS | 10.5+ | 轻量级实时操作系统，任务调度 |
| BLE 协议栈 | SoftDevice S132/S140 | — | Nordic 官方 BLE 5.0 协议栈 |
| 开发语言 | C | C99 | 嵌入式固件开发 |
| 开发工具 | nRF Connect SDK | 2.5+ | Nordic 官方 SDK + VS Code 插件 |
| 烧录工具 | nRFjprog + J-Link | 10+ | 固件烧录和调试 |
| 加密库 | mbed TLS | 3.5+ | AES-128-CCM / ECDH 密钥协商 |
| OTA 库 | Nordic DFU (Device Firmware Upgrade) | — | 官方 OTA 框架 |

## 2. 移动端 App

| 组件 | 选型 | 版本 | 说明 |
|------|------|------|------|
| 跨平台框架 | React Native | 0.73+ | iOS + Android 一套代码 |
| 语言 | TypeScript | 5.3+ | 类型安全 |
| BLE 库 | react-native-ble-plx | 3.1+ | 底层 BLE 通信封装 |
| 加密库 | react-native-aes-crypto | 2.1+ | AES-128 加密/解密 |
| 状态管理 | Zustand | 4.5+ | 轻量状态管理 |
| UI 框架 | NativeWind (Tailwind for RN) | — | 统一 UI 风格 |
| 本地存储 | MMKV | 3.0+ | 高性能 KV 存储，存密钥/配置 |

## 3. 服务端（辅助）

| 组件 | 选型 | 版本 | 说明 |
|------|------|------|------|
| 运行时 | Node.js | 20 LTS | API 服务 |
| 框架 | Express.js | 4.18+ | RESTful API |
| 数据库 | PostgreSQL | 16 | 用户/设备/权限管理 |
| ORM | Prisma | 5.8+ | 类型安全 ORM |
| 鉴权 | JWT + bcrypt | — | 用户认证 |
| 消息队列 | Redis (Pub/Sub) | 7.2+ | 设备状态推送 |

## 4. 硬件驱动层

| 组件 | 选型 | 说明 |
|------|------|------|
| 锁控板通信 | GPIO + SPI | 控制电机驱动 |
| 电机驱动 | DRV8833 / A4950 | 有刷直流电机驱动 |
| 状态传感器 | Hall Sensor (A1324) | 锁舌到位检测 |
| 指示灯 | WS2812B / 普通 LED | 状态指示 |
| 蜂鸣器 | 有源蜂鸣器 | 开锁/告警提示音 |
| 电池管理 | TP4056 + INA219 | 充电管理 + 电量监测 |

## 5. 加密与通信

| 协议 | 方案 | 说明 |
|------|------|------|
| BLE Pairing | LE Secure Connections (LESC) | BLE 4.2+ 安全配对 |
| 密钥交换 | ECDH (P-256) | 非对称密钥协商 |
| 对称加密 | AES-128-CCM | 应用层数据加密 |
| 消息完整性 | HMAC-SHA256 | 消息防篡改 |
| 防重放 | 单调递增 Nonce + 时间戳 | 4 字节 Nonce + Unix 时间戳 |
| 固件签名 | ECDSA (P-256) | OTA 固件包签名验证 |

## 6. OTA 升级

| 组件 | 方案 | 说明 |
|------|------|------|
| OTA 协议 | Nordic Secure DFU | 官方安全固件升级协议 |
| 传输方式 | BLE GATT 分包传输 | 最大 MTU 247 bytes |
| 升级包格式 | ZIP (manifest + binary + sig) | 含版本号和签名 |
| 回退机制 | Dual Bank + 启动校验 | 升级失败自动回退到旧版本 |
| App OTA 支持 | react-native-ble-dfu | DFU 流程封装 |

## 7. 开发 & CI/CD

| 工具 | 用途 |
|------|------|
| VS Code + nRF Connect 插件 | 固件 IDE |
| nRF52 DK | 开发板 |
| Wireshark + nRF Sniffer | BLE 抓包分析 |
| J-Link RTT | 实时日志输出 |
| GitHub Actions | CI：编译 + 单元测试 |

## 8. 依赖关系图

```
┌─────────────────────────────────────────────────┐
│                    移动端 App                     │
│  React Native + react-native-ble-plx             │
│  (BLE 连接 / 加密 / UI / 本地存储)                 │
└───────────────┬─────────────────────────────────┘
                │ BLE 5.0 GATT (加密)
                ▼
┌─────────────────────────────────────────────────┐
│              锁控板固件 (nRF52)                    │
│  FreeRTOS + SoftDevice + 电机驱动 + 传感器         │
│  (配对 / AES-CCM / OTA DFU / 锁控逻辑)            │
└───────────────┬─────────────────────────────────┘
                │ WiFi / 4G 模块 (可选)
                ▼
┌─────────────────────────────────────────────────┐
│                   后端服务                         │
│  Node.js + Express + PostgreSQL + Redis           │
│  (用户管理 / 设备管理 / 远程开锁 / 日志)             │
└─────────────────────────────────────────────────┘
```
