# ARCHITECTURE — BLE 智能锁固件开发与 App 对接

## 项目信息

| 字段 | 值 |
|------|------|
| 项目 ID | P-2026-00011 |
| 关联任务 | T-2026-00044 |

---

## 1. 系统总览

```
┌──────────────┐      BLE 5.0 GATT      ┌───────────────────────────┐
│              │   (AES-128-CCM 加密)     │    BLE 智能锁固件          │
│   移动端 App  │◄──────────────────────►│  (nRF52832/52840 SoC)     │
│   iOS/Android │                        │  FreeRTOS + SoftDevice    │
│              │                        │  ┌─────────────────────┐  │
└──────┬───────┘                        │  │  BLE 服务 & 特征值   │  │
       │                                │  ├─────────────────────┤  │
       │ HTTPS                          │  │  锁控逻辑引擎        │  │
       ▼                                │  ├─────────────────────┤  │
┌──────────────┐                        │  │  加密通信模块        │  │
│  后端 API    │                        │  ├─────────────────────┤  │
│ Node.js/PSQL │                        │  │  OTA DFU 模块       │  │
└──────────────┘                        │  ├─────────────────────┤  │
                                        │  │  驱动 & 传感器适配   │  │
                                        │  └─────────────────────┘  │
                                        └───────────────────────────┘
```

## 2. 固件架构（锁控板）

### 2.1 模块划分

```
firmware/
├── src/
│   ├── main.c                  # 入口，FreeRTOS 任务创建
│   ├── ble/
│   │   ├── ble_service.c       # GATT 服务定义（UUID、特征值）
│   │   ├── ble_handler.c       # 连接/断开/配对事件处理
│   │   ├── ble_crypto.c        # AES-128 加解密、ECDH 密钥协商
│   │   └── ble_ota.c           # OTA DFU 处理
│   ├── lock/
│   │   ├── lock_engine.c       # 锁控核心逻辑（状态机）
│   │   ├── lock_motor.c        # 电机驱动（GPIO 控制）
│   │   ├── lock_sensor.c       # Hall 传感器读取（锁舌检测）
│   │   └── lock_log.c          # 开锁日志存储（Flash 环形缓冲）
│   ├── power/
│   │   ├── power_manager.c     # 低功耗管理（休眠/唤醒策略）
│   │   └── battery_monitor.c   # 电量监测（INA219 ADC 采样）
│   └── crypto/
│       ├── key_store.c         # 密钥安全存储（硬件 Flash 保护区）
│       ├── hmac_verify.c       # HMAC-SHA256 消息验证
│       └── anti_replay.c       # 防重放检查（Nonce + 时间戳）
└── include/
    ├── ble_service.h
    ├── lock_engine.h
    ├── crypto.h
    └── power_manager.h
```

### 2.2 BLE GATT 服务定义

| 服务 UUID | 名称 | 特征值 |
|-----------|------|--------|
| `0000FE15` (自定义) | Lock Control Service | 见下表 |

| 特征值 UUID | 名称 | 属性 | 说明 |
|-------------|------|------|------|
| `0x2A00` | Device Name | Read | 设备名称 |
| `0x2A01` | Appearance | Read | BLE 外观类型 |
| `0xF001` | Lock Status | Notify | 锁状态变化通知（2B：锁舌状态 + 电池百分比） |
| `0xF002` | Lock Command | Write + Auth | 开锁/上锁/查询命令（AES 加密） |
| `0xF003` | Lock Log | Read + Notify | 历史开锁日志 |
| `0xF004` | Key Management | Write + Auth | 密钥分发/撤销/轮换 |
| `0xF005` | OTA Control | Write | DFU 控制点 |
| `0xF006` | OTA Data | Write | DFU 固件数据包 |

### 2.3 锁控状态机

```
[INIT] ──自检通过──► [STANDBY] ──收到开锁指令──► [UNLOCKING]
  │                    │                          │
  │                    │                          ▼
  │                    │                     [LOCKED] ──超时──► [STANDBY]
  │                    │                          │
  │                    │                          ▼
  │                    │                     [LOCKED_FAIL] ──3次失败──► [ALARM]
  │                    │
  │                    └──电量低──► [LOW_POWER] ──充电──► [STANDBY]
  │
  └──自检失败──► [ERROR]
```

| 状态 | 说明 | 电流消耗 |
|------|------|----------|
| INIT | 上电自检 | 15mA |
| STANDBY | 广播/待机 | 20μA（休眠）/ 1mA（广播） |
| UNLOCKING | 电机运转 | 300mA（3s） |
| LOCKED | 已上锁 | 20μA |
| ALARM | 告警（蜂鸣+LED） | 25mA |
| LOW_POWER | 低电量模式 | 5μA |
| ERROR | 故障状态 | 1mA |

### 2.4 低功耗策略

| 场景 | 策略 |
|------|------|
| 待机 | 关闭所有非必要外设，进入 System OFF，广播间隔 1s |
| 连接 | 保持连接间隔 30ms，无操作 30s 后断开 |
| 开锁 | 电机运转 3s 后自动停止，返回 STANDBY |
| 唤醒 | 电容触摸唤醒 / NFC 唤醒 / 定时广播唤醒 |

## 3. App 架构

```
app/
├── src/
│   ├── screens/
│   │   ├── HomeScreen.tsx         # 主页（设备列表 + 快捷开锁）
│   │   ├── LockScreen.tsx         # 锁详情（状态/日志/设置）
│   │   ├── KeyScreen.tsx          # 密钥管理（添加/撤销/分享）
│   │   └── SettingsScreen.tsx     # 设备设置
│   ├── services/
│   │   ├── bleManager.ts          # BLE 连接管理（扫描/连接/重连）
│   │   ├── lockService.ts         # 锁控指令封装（加密/发送/等待响应）
│   │   ├── otaService.ts          # OTA 升级流程
│   │   └── keyManager.ts          # 本地密钥管理（MMKV）
│   ├── crypto/
│   │   ├── aes.ts                 # AES-128-CCM 封装
│   │   ├── ecdh.ts                # ECDH 密钥协商
│   │   └── hmac.ts                # HMAC-SHA256
│   ├── store/
│   │   ├── deviceStore.ts         # 设备状态（Zustand）
│   │   └── logStore.ts            # 开锁日志
│   └── utils/
│       ├── retry.ts               # BLE 操作重试逻辑
│       └── format.ts              # 时间/状态格式化
├── assets/
│   └── icons/
└── App.tsx
```

### 3.1 BLE 连接管理流程

```
[启动 App]
    │
    ▼
[扫描设备] ──广播过滤──► [匹配设备名]
    │
    ▼
[建立连接] ──LE Secure Pairing──► [ECDH 密钥交换]
    │
    ▼
[会话建立] ──AES-128 密钥派生──► [加密通信]
    │
    ├── [开锁指令] ──加密发送──► [等待响应] ──成功──► [更新 UI]
    ├── [状态订阅] ──注册 Notify──► [状态变化推送]
    └── [断线重连] ──指数退避──► [自动重连]
```

## 4. 后端架构

```
backend/
├── src/
│   ├── routes/
│   │   ├── auth.ts              # 用户注册/登录/JWT
│   │   ├── devices.ts           # 设备绑定/解绑/远程开锁
│   │   ├── keys.ts              # 密钥分发/撤销/临时密钥
│   │   └── logs.ts              # 开锁日志查询/导出
│   ├── services/
│   │   ├── deviceService.ts     # 设备状态管理
│   │   ├── keyService.ts        # 密钥生命周期管理
│   │   └── notificationService.ts # 推送通知（APNs/FCM）
│   ├── middleware/
│   │   ├── auth.ts              # JWT 鉴权
│   │   └── rateLimit.ts         # 频率限制
│   └── db/
│       ├── schema.prisma        # Prisma 数据模型
│       └── seed.ts              # 测试数据
└── prisma/
```

### 4.1 数据模型

```prisma
model User {
  id          String   @id @default(uuid())
  phone       String   @unique
  name        String
  devices     Device[]
  createdAt   DateTime @default(now())
}

model Device {
  id            String   @id @default(uuid())
  name          String
  macAddress    String   @unique
  firmwareVer   String
  batteryLevel  Int
  status        String   // online, offline, low_battery
  owner         User     @relation(fields: [ownerId], references: [id])
  ownerId       String
  keys          LockKey[]
  logs          LockLog[]
  createdAt     DateTime @default(now())
}

model LockKey {
  id          String   @id @default(uuid())
  device      Device   @relation(fields: [deviceId], references: [id])
  deviceId    String
  keyType     String   // admin, user, temporary
  expiresAt   DateTime?
  revoked     Boolean  @default(false)
  createdAt   DateTime @default(now())
}

model LockLog {
  id          String   @id @default(uuid())
  device      Device   @relation(fields: [deviceId], references: [id])
  deviceId    String
  action      String   // unlock, lock, key_add, key_revoke, ota
  operator    String   // app, fingerprint, password, nfc, key
  timestamp   DateTime @default(now())
}
```

## 5. 加密通信协议

### 5.1 消息帧格式

```
┌────────┬────────┬────────┬────────┬────────┬────────┬────────┐
│ Header │ Nonce  │ Cmd    │ Payload│ HMAC   │ Footer │
│ (2B)   │ (4B)   │ (1B)   │ (N B)  │ (4B)   │ (2B)   │
└────────┴────────┴────────┴────────┴────────┴────────┘

Header:  0xAA 0x55 (魔数)
Nonce:   单调递增计数器 + 设备 ID 哈希
Cmd:     命令码（0x01=开锁, 0x02=上锁, 0x03=查询状态, 0x04=日志, 0x05=密钥管理, 0x06=OTA）
Payload: AES-128-CCM 加密载荷
HMAC:    HMAC-SHA256 前 4 字节
Footer:  0x0D 0x0A
```

### 5.2 密钥协商流程

```
App                                    锁控板
 │                                      │
 │──── Pairing Request (LESC) ────────► │
 │◄─── Pairing Response ─────────────── │
 │                                      │
 │──── ECDH Public Key ───────────────► │
 │◄─── ECDH Public Key ──────────────── │
 │                                      │
 │──── 计算共享密钥 ──────► AES-128 会话密钥 │
 │                                      │
 │──── 验证加密消息 ───────────────────► │
 │◄─── 验证通过/失败 ────────────────── │
```

## 6. OTA 升级流程

```
App                                    锁控板
 │                                      │
 │──── 检查新版本 ────────────────────► │ (查询当前版本)
 │◄─── 当前版本号 ───────────────────── │
 │                                      │
 │──── DFU Init ──────────────────────► │ (进入 DFU 模式)
 │                                      │
 │──── 传输固件包（分包） ─────────────► │ (每包 247 bytes)
 │◄─── ACK / NACK ───────────────────── │
 │                                      │
 │──── DFU Complete ──────────────────► │ (触发重启 + 签名验证)
 │                                      │
 │◄─── 新版本号 ─────────────────────── │ (升级成功)
```

## 7. 接口清单

### 7.1 固件 → App 接口

| 接口 | 特征值 | 类型 | 说明 |
|------|--------|------|------|
| 获取锁状态 | 0xF001 Notify | 2B | 锁舌状态(1B) + 电池%(1B) |
| 开锁响应 | 0xF002 Write Resp | 2B | 结果码(1B) + 错误码(1B) |
| 读取日志 | 0xF003 Read | 可变 | 日志条目数组 |
| 密钥列表 | 0xF004 Read | 可变 | 密钥 ID + 类型 + 状态 |

### 7.2 后端 → App 接口

| 接口 | 方法 | 路径 | 说明 |
|------|------|------|------|
| 注册/登录 | POST | /api/auth/register, /api/auth/login | 手机号 + 验证码 |
| 设备列表 | GET | /api/devices | 用户绑定的设备 |
| 远程开锁 | POST | /api/devices/:id/unlock | 通过网关远程开锁 |
| 添加密钥 | POST | /api/devices/:id/keys | 分发新密钥 |
| 撤销密钥 | DELETE | /api/devices/:id/keys/:keyId | 密钥作废 |
| 开锁日志 | GET | /api/devices/:id/logs | 分页查询 |

## 8. 部署架构

```
┌─────────────────────────────────────┐
│         Docker Compose               │
│  ┌─────────┐  ┌─────────┐           │
│  │  API    │  │  Redis  │           │
│  │ (Node)  │  │ (缓存)  │           │
│  └────┬────┘  └────┬────┘           │
│       └─────┬──────┘                │
│             │                        │
│       ┌─────▼─────┐                 │
│       │ PostgreSQL │                 │
│       └────────────┘                 │
└─────────────────────────────────────┘
```

## 9. 安全设计

| 威胁 | 防护措施 |
|------|----------|
| BLE 嗅探 | AES-128-CCM 加密所有应用层数据 |
| 重放攻击 | Nonce 单调递增 + 时间戳窗口（±5s） |
| 中间人 | LESC 配对 + ECDH 密钥协商 |
| 暴力破解 | 连续 3 次错误锁定 5 分钟，触发告警 |
| 固件篡改 | OTA 签名验证（ECDSA P-256） |
| 密钥泄露 | 临时密钥自动过期，撤销后立即失效 |
| 物理拆卸 | 防拆开关触发告警 + 云端通知 |
