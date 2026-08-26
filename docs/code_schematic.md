# 固件工作原理

## 1. 概述

本固件运行于 **ESP32-S3** 微控制器，驱动 **ST7789 320×240 彩色 TFT 屏幕**，
通过 **BLE（蓝牙低功耗）** 读取 **ANT / JK / JBD / 彦阳** 四种电动车电池保护板
（BMS）的实时状态，并在屏幕上以两种模式显示：

- **Factory UI**：编译在固件中的出厂仪表盘，由 `MainPage` + `CellPage` 组成。
- **Custom UI（动态主题）**：用户通过 Web 门户上传自定义 `.bmsui` 主题文件，
  经 **BMSUI 解析器** 验证后，固件在重启时动态构建 LVGL 控件树并渲染。

固件**只读**——不发送保护参数修改或 MOS 控制命令。

---

## 2. 硬件目标

| 组件 | 规格 |
|---|---|
| MCU | ESP32-S3（Xtensa LX7 双核 240 MHz） |
| 屏幕 | ST7789 320×240 横屏，SPI 接口 |
| 外部 RAM | PSRAM（8 MB Octal，N16R8 模组） |
| 存储 | SPIFFS（960 KB 分区，用于存放自定义主题包） |
| 按键 | GPIO 6，低电平有效，单击/三击/长按 10 秒 |
| 背光 | GPIO 7，PWM 5 kHz |

---

## 3. 整体架构

```
┌─────────────────────────────────────────────────────────────┐
│                     application.cpp                         │
│           begin() / loop() —— 主调度器                       │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  ┌───────────────────┐   ┌──────────────────────────────┐   │
│  │   DisplayUi       │   │   BmsBleService              │   │
│  │   ┌─────────────┐ │   │   ┌────────────────────────┐ │   │
│  │   │ Factory UI  │ │   │   │  BLE 状态机             │ │   │
│  │   │ MainPage    │ │   │   │  Idle/Scanning/Connecting│ │   │
│  │   │ CellPage    │ │   │   │  Online/Disconnecting   │ │   │
│  │   └─────────────┘ │   │   └────────┬───────────────┘ │   │
│  │   ┌─────────────┐ │   │            │                  │   │
│  │   │ DynamicUi   │ │   │   ┌────────▼───────────────┐ │   │
│  │   │ (Custom UI) │ │   │   │  ANT / JK / JBD / YY  │ │   │
│  │   └─────────────┘ │   │   │    协议解码器 (.feed)   │ │   │
│  └───────────────────┘   │   └────────────────────────┘ │   │
│                          └──────────────────────────────┘   │
│  ┌───────────────────┐   ┌──────────────────────────────┐   │
│  │  DisplayPower     │   │  ConfigWebPortal             │   │
│  │  Manager          │   │  SoftAP + WebServer +        │   │
│  │  (自动调暗/休眠)   │   │  BMSUI 主题上传/管理         │   │
│  └───────────────────┘   └──────────────────────────────┘   │
│                          ┌──────────────────────────────┐   │
│  ┌───────────────────┐   │  BmsDataStore                │   │
│  │  ButtonController │   │  (线程安全的数据发布/消费)    │   │
│  └───────────────────┘   └──────────────────────────────┘   │
│                          ┌──────────────────────────────┐   │
│                          │  RuntimeSettings             │   │
│                          │  Preferences 持久化          │   │
│                          └──────────────────────────────┘   │
├─────────────────────────────────────────────────────────────┤
│               src/ 目录结构                                   │
│  core/      数据存储、设置、日志、时间工具                     │
│  protocol/  ANT/JK/JBD/彦阳 4 种 BMS 协议帧解析              │
│  services/  BLE 状态机 + Web 配置门户                        │
│  presentation/  Factory UI 页面 + 仪表盘状态逻辑 + 测试模式   │
│  ui_runtime/ BMSUI 解析器、动态 UI 构建、数据绑定、包存储    │
│  power/      屏幕电源逻辑（车辆活动检测 + 调暗/休眠策略）     │
│  input/      按键消抖和单击/三击/长按检测                     │
│  storage/    配对设备 Preferences 存储                        │
│  ui/         LVGL 内置资源（字体、图片、UI 描述文件）         │
└─────────────────────────────────────────────────────────────┘
```

---

## 4. 模块详解

### 4.1 入口与主循环 — `src/application.cpp`

`setup()` / `loop()` 入口在 `src/main.cpp`（对应原 `firmware.ino`），
主循环栈设为 20480 字节（覆盖 Web 配置和主题上传的调用栈需求）。

**`Application::begin()` 初始化顺序：**

1. **Serial** (115200 baud) + **DiagnosticLog**（互斥锁保护串口输出）
2. **RuntimeSettings**（从 Preferences 加载：里程系数、Web 密码、显示电源设置）
3. 打印启动横幅（固件名称/版本、PSRAM 大小、复位原因、测试模式状态）
4. **BmsDataStore** 重置
5. **ButtonController** 初始化（GPIO 6，低电平，消抖 35 ms）
6. **DisplayUi::begin()** → LVGL 初始化 + 加载 Factory UI 或 Custom UI
7. **DisplayPowerManager** 开始
8. 非测试模式：**BmsBleService::begin()** → 读取配对设备 → 如果无配对目标则直接进入 Web 配置门户（带 BLE 扫描）；否则先启动 BLE 连接，延迟启动恢复门户

**`Application::loop()` 主循环（每次迭代约 2ms yield）：**

1. 按键处理（`userButton.loop()`）
2. 显示更新（`DisplayUi::loop()` → LVGL 定时器 + 页面刷新）
3. Web 门户循环（`ConfigWebPortal::loop()` → DNS + HTTP 请求处理）
4. BLE 状态机（`BmsBleService::loop()` → 回调事件 → 状态转换 → 请求/响应）
5. 门户启动管理（`servicePortalLaunch()` → BLE 扫描完成后启动 SoftAP）
6. 电源管理（`DisplayPowerManager::loop()` → 车辆活动检测 → 调暗/休眠）
7. BMS 数据消费（`BmsDataStore::consumePendingUpdate()` → 更新 UI + 电源观察）

### 4.2 显示层 — `src/display_ui.cpp` + `src/ui/`

**初始化：**

1. 配置硬件引脚（CS/RST/DC/MOSI/SCLK/MISO 和背光 PWM）
2. 复位 ST7789 面板（高低电平脉冲序列）
3. 初始化 **LovyanGFX** 设备（`LGFX_Device`，总线 SPI2_HOST，频率 10 MHz）
4. 设置旋转方向、颜色字节序、背光亮度
5. 初始化 LVGL（`lv_init()` → 显示驱动注册 → 刷新缓冲区分配）
6. 调用 `ui_init()` 加载 Factory UI 资源
7. 尝试 `DynamicUi::begin()`：
   - 成功 → 加载自定义主题屏幕，销毁 Factory UI 对象
   - 失败 → 启动 `MainPage::begin()`（Factory UI）
8. 标记启动健康（清除启动保护守卫）

**运行阶段：**

| 方法 | 调用者 | 行为 |
|---|---|---|
| `loop()` | applicaton loop | LVGL tick 递增 + timer_handler 刷新 |
| `update(data)` | BMS 数据到达 | 同时更新 MainPage + DynamicUi + CellPage |
| `togglePage()` | 按键单击 | 切换 Main ↔ Cells 页面 |
| `setEnabled()` | 电源管理 | 屏幕开关（不销毁对象，仅控制显示） |

**Factory UI 页面：**

- **MainPage**：仪表盘（SOC 弧形进度条、电压/电流/功率/里程等数值标签、连接状态图标）
- **CellPage**：电芯电压列表（`CellPage::update()` 按行渲染电芯数据）

### 4.3 BLE 通信层 — `src/services/bms_ble_service.cpp` + `src/protocol/`

#### 4.3.1 BLE 状态机

```
 Idle ──→ Scanning ──→ Connecting ──→ Online ──→ Disconnecting ──→ Idle
                  │          │                             │
                  └── FastReconnectWait ───────────────────┘
```

- **Idle**：未连接，等待定时器触发扫描或重连
- **Scanning**：扫描指定品牌 BMS 的 BLE 广播（按名称前缀匹配：`ANT@` / `JK` / `JBD` / `xiaoxiang` / `YY` / `B40`）
- **Connecting**：NimBLE 连接对方设备
- **Online**：已连接，发现服务和特征（UUID 匹配），订阅通知 → 发送状态请求帧 → 接收通知 → 解析协议
- **Disconnecting**：主动断开或错误断开后的清理

**自动重连**：丢失连接后先尝试快速重连（`fastReconnectAttempts`，默认 1 次），
失败后回退到全扫描模式。

#### 4.3.2 协议解码器

四种 BMS 协议均继承"流式帧解析"模式：

```
BLE 通知（字节流）→ 帧缓冲区（MaxFrameSize=384）→ processBufferedFrames()
    → processFrame() → parseStatusFrame() → 回调 onStatusData()
```

| 协议 | 帧特征 | 解码器 |
|---|---|---|
| ANT | 0x7E 分隔，校验和 0x55 0xAA 结尾 | `AntProtocolDecoder` |
| JK | 可变帧长，按版本号/字段数评分 | `JkProtocolDecoder`（支持 24S/32S 自动检测） |
| JBD | 0xDD 0x... 0x77，多命令（0x03/0x04/0x05） | `JbdProtocolDecoder`（需要 0x03+0x04 两帧合成完整数据） |
| 彦阳 | Modbus RTU，CRC16 | `YanyangProtocolDecoder`（请求 + 响应配对） |

**轮询策略**：ANT 和 JK 通过 BLE 写特征发送请求帧，BMS 以通知回复；
JBD 需要额外发送 0x04 命令读取电芯电压；彦阳通过 Modbus 请求。
在线状态定时发送 `sendStatusRequestForActiveType()`（周期 3s）。

#### 4.3.3 通知队列

BLE 通知到达 `notifyCallback()` → 入队环形缓冲区（1024 bytes）→
`loop()` 中 `processNotificationQueue()` 出队并喂给对应的协议解码器。

---

### 4.4 BMSUI 动态 UI 系统 — `src/ui_runtime/`

这是固件的"自定义主题"引擎，支持从 BMSUI v1 到 v7 的协议版本。

#### 4.4.1 主题文件格式（BMSUI）

`.bmsui` 文件是**纯文本行协议**（`bmsui_protocol.h`）：

```
// 元数据（版本、屏幕尺寸、背景色、标题）
VERSION|7
WIDTH|320
HEIGHT|240
BGCOLOR|000000
TITLE|My Dashboard

// 控件定义（类型、位置、尺寸、颜色、数据绑定）
WIDGET|Label|0|socLabel|10|20|100|30|ffffff|...|soc|0

// 资源声明（图片/A8 蒙版/字体图集，嵌入 ASSETDATA 或 ASSETB64）
ASSET|icon_bat|48|48|RGB565A8|9216
ASSETDATA|icon_bat|ffd8ffe000104a464...
```

**控件类型**：Label、Rect、Line、Arc、Bar、StateImage、Image、TextImage、Shape、VectorShape、GlyphValue

**数据绑定**：SOC、总电压、电流、功率、剩余容量、总容量、里程、MOS 温度、电芯压差、充放电 MOS 状态、均衡状态等（`BmsUi::DataId`）。

#### 4.4.2 解析 — `bmsui_parser.cpp`

`BmsUi::parse(Stream &stream, Package &package, ParseResult &result)`：
逐行解析，`|` 分隔字段，构建 `Package` 结构体（widgets / assets 数组，最大 48 个控件 + 48 个资源）。

#### 4.4.3 存储 — `ui_package_store.cpp`

**SPIFFS 文件路径**：

| 路径 | 用途 |
|---|---|
| `/ui.bmsui` | 当前生效的主题包 |
| `/ui.tmp` | 上传暂存（Web 上传时会写入此文件） |
| `/ui.bak` | 提交前备份旧的 `/ui.bmsui` |
| `/ui.bad` | 被隔离的崩溃主题包 |

**关键流程：**

- **`beginStaging(file)`**：打开 `/ui.tmp` 供 Web 上传写入
- **`commitStaging(result)`**：校验 `/ui.tmp` → 备份 `/ui.bmsui` → 复制 `/ui.tmp` 到 `/ui.bmsui` → 重新挂载 SPIFFS 验证 → 清除暂存
- **`recoverPendingPackage(result)`**：启动时如果 `/ui.bmsui` 无效，尝试从 `/ui.tmp` 或 `/ui.bak` 恢复
- **`quarantineCurrentPackage()`**：如果自定义 UI 在激活时崩溃，隔离它（下次启动跳过）
- **`resetCustomPackage()`**：删除所有主题文件，回到 Factory UI

#### 4.4.4 动态 UI 加载 — `dynamic_ui.cpp`

**`DynamicUi::begin()` 加载流程：**

1. 挂载 SPIFFS（`UiPackageStore::begin()`）
2. **PSRAM 检查**：要求 PSRAM ≥ 1 MB（`psramUsable()`），否则直接返回 false（主题包保留不隔离）
3. **崩溃保护守卫**：检查 RTC 内存中的 `uiBootGuardMagic`，如果上次激活中断则隔离当前包
4. 检查是否有自定义包（`UiPackageStore::hasCustomPackage()`）
5. **解析包**（`UiPackageStore::load()`）
6. **资源预算检查**：包内资源总字节数 ≤ 可用运行时限制（有 PSRAM = 196608，无 PSRAM = 98304）
7. **构建屏幕**（`buildScreen()`）：创建 LVGL 控件树（屏幕 → 控件 → 子控件）
8. **标记激活**：设置 `customActive = true`，渲染首帧

**`buildScreen()` 创建控件**：
根据 `Package.widgets` 数组逐个创建 LVGL 控件（`lv_label_create`、`lv_arc_create`、`lv_bar_create`、`lv_img_create`、`lv_line_create`、`lv_obj_create`），设置位置、大小、颜色、圆角、透明度、字体、数据绑定等。

**`render()` 更新**：
遍历运行时控件，根据 `UiDataProvider` 提供的数据绑定值更新控件状态（标签文本、弧角度、进度条宽度、图片切换、状态灯颜色等）。

**资源缓存**：`RuntimeAssetCacheEntry` 数组缓存解码后的图片/字体资源，避免重复解析。

---

### 4.5 Web 配置门户 — `src/services/config_web_portal.cpp`

**启动条件**：长按按键 10 秒 / 首次启动无配对设备 / BLE 连接失败后恢复。

**流程**：

1. **BLE 预扫描**：扫描指定品牌的 BMS 设备（`BmsBleService::requestConfigurationScan()`），
   扫描完成后启动 SoftAP
2. **SoftAP**：SSID = "无用脑洞研究所"，IP = 192.168.4.1，通道 6，单客户端
3. **DNS 捕获门户**：拦截所有域名请求，重定向到 `/`
4. **HTTP API**（`WebServer` 端口 80）：

| 端点 | 用途 |
|---|---|
| `/` | 配置页面（需登录） |
| `/login` / `/logout` | 管理员密码认证 |
| `/api/status` | 固件/PSRAM/显示设置状态 |
| `/api/devices` | BLE 扫描结果列表 |
| `/api/save` | 保存 BMS 配对目标（type + address） |
| `/api/display` | 修改自动调暗/休眠设置 |
| `/api/rescan` | 重新扫描 BMS |
| `/api/password` | 修改 Web 管理密码 |
| `/api/ui/info` | 自定义主题状态（大小、是否有效、PSRAM 等） |
| `/api/ui/download` | 下载当前 `/ui.bmsui` |
| `/api/ui/upload` | 上传新主题（写入 `/ui.tmp`） |
| `/api/ui/commit-status` | 检查提交状态（pending/running/success/failed） |
| `/api/ui/recover` | 恢复暂存/备份主题 |
| `/api/ui/reset` | 删除自定义主题，回到 Factory UI |
| `/api/test-mode` | 仪表盘测试模式（开发用） |

**主题上传事务**：
```
Upload → /ui.tmp (staging) → 延迟提交 → commitStaging()
    → 校验 → 备份 /ui.bmsui → 复制 /ui.tmp → /ui.bmsui
    → SPIFFS 重挂载验证 → 重启
```

---

### 4.6 数据模型 — `BmsData` + `BmsDataStore`

**`BmsData`**（`bms_model.h`）：统一数据结构，包含：

- 电芯电压数组（32 个 float）、温度数组（6 个 int16_t）
- 总电压、电流、功率、SOC、SOH
- 充放电 MOS 状态、均衡状态
- 容量（总/剩余/循环）、运行时间、循环次数
- 电芯统计（最高/最低/平均/压差）
- 硬件/软件版本号

**`BmsDataStore`**（`core/bms_data_store.cpp`）：线程安全的生产者-消费者模型。
- 协议解码器回调 → `publishStatus(data)` → 加锁写入内部缓冲区
- 主循环 → `consumePendingUpdate(data)` → 加锁读取最新数据 → 更新 UI

---

### 4.7 配置持久化 — `RuntimeSettings` + `PairedDeviceStore`

**`RuntimeSettings`**（`core/runtime_settings.cpp`）：
使用 Arduino `Preferences` 库（NVS 分区）存储：
- 里程系数（km/Ah）
- Web 管理密码
- 显示电源设置（自动开关、休眠分钟数）
- 仪表盘测试模式开关
- 启动时配置扫描请求

**`PairedDeviceStore`**（`storage/paired_device_store.cpp`）：
使用 `Preferences` 存储 BMS 配对信息（类型 + BLE 地址 + 设备名称）。

---

### 4.8 显示电源管理 — `DisplayPowerLogic` + `DisplayPowerManager`

**车辆活动检测**（`display_power_logic.cpp`）：

```
VehicleActivityDetector
  → observe(currentA, timestamp)  收集电流样本
  → classify(nowMs) → Stationary / Active / Unknown
```

基于电流的滤波值、波动幅度和新鲜度判断车辆状态。

**显示策略**（`DisplayPowerPolicy`）：

```
Bright → (车辆静止累计 ≥ dimDelay) → Dimmed → (更久无活动) → AutoSleeping
  ↑                                                            |
  └─────── notifyUserInteraction / 车辆活动 ──────────────────┘
```

**`DisplayPowerManager`** 协调逻辑与硬件：
- 调用 `VehicleActivityDetector.observe()` 喂电流数据
- 调用 `DisplayPowerPolicy.update()` 决策屏幕状态
- 调用 `DisplayUi::setBrightnessPercent()` 和 `DisplayUi::setEnabled()` 控制背光
- 三击按键可手动切换屏幕开关

---

### 4.9 按键控制 — `ButtonController`

`ButtonController`（`input/button_controller.cpp`）实现三态识别：

| 操作 | 条件 | 回调 |
|---|---|---|
| 单击 | 按下释放后 1s 内无更多点击 | `handleSingleClick()` → 切换页面 |
| 三击 | 1s 内连续点击 3 次 | `handleTripleClick()` → 手动切换屏幕开关 |
| 长按 | 持续按下 10 秒 | `handleLongPress()` → 请求配置门户 |

---

## 5. 数据流

```
BMS 保护板
    │
    │ BLE 广播
    ▼
BmsBleService::begin() → NimBLE 扫描 → 按名称匹配
    │
    ├── 连接 → 发现服务/特征 → 订阅通知
    │       │
    │       │ 发送状态请求帧（周期 3s，或 JBD 额外 0x04 帧）
    │       │
    │       ▼
    │   notifyCallback() → 通知环形缓冲区
    │       │
    │       ▼
    │   processNotificationQueue() → 协议解码器 .feed()
    │       │
    │       ▼
    │   onStatusData(BmsData) → BmsDataStore::publishStatus()
    │       │
    │       ▼
    │   consumePendingUpdate() → DisplayUi::update(data)
    │                               ├── MainPage::update()
    │                               ├── DynamicUi::update()
    │                               └── CellPage::update()
    │
    └── 无配对设备 / 连接失败 → Web 门户
            │
            ▼
        ConfigWebPortal::begin()
            ├── BLE 预扫描 → 显示扫描结果列表
            ├── 用户选择 BMS → /api/save → 配对存储
            └── 用户上传主题 → /api/ui/upload → /ui.tmp
                    │
                    ▼
                commitStaging() → /ui.bmsui → 重启
                    │
                    ▼
                DynamicUi::begin() → 解析 → 构建 LVGL 屏幕 → 显示
```

---

## 6. 关键状态机

### 6.1 BLE 链路状态

```
Idle
  │
  ├── [有缓存目标] → startScan(Reconnect) → Scanning
  ├── [无缓存目标] → 等待配置扫描请求
  │
Scanning
  │
  ├── [发现目标] → connectFreshTarget() / connectCachedClient() → Connecting
  ├── [超时/失败] → scheduleReconnectScan() → Idle
  │
Connecting
  │
  ├── [成功] → discoverAndSubscribe() → Online
  ├── [失败] → 清理 GATT 缓存 → scheduleReconnectScan() → Idle
  │
Online
  │
  ├── [有有效状态] → 定时发送状态请求 → 接收通知 → 解析
  ├── [JBD 类型] → 额外发送 0x04 电芯请求
  ├── [状态超时] → attemptInPlaceStreamRecovery() → 继续 Online
  ├── [硬超时 45s] → requestConnectionRecovery() → Disconnecting
  ├── [断开事件] → handleDisconnectEvent() → FastReconnectWait / Idle
  │
FastReconnectWait
  │
  ├── [快速重连成功] → Online
  └── [快速重连失败] → startScan() → Scanning
```

### 6.2 显示电源状态

```
Bright ────→ Dimmed ────→ AutoSleeping
  ↑              ↑              │
  └──────────────┴──────────────┘
  (notifyUserInteraction / 车辆活动检测到活跃)

  ManualSleeping ←── 三击按键切换
  （完全关闭屏幕，跳过自动调暗/休眠）
```

---

## 7. 主题上传/激活完整流程

```
用户长按按键 10 秒
    │
    ▼
requestScannedPortal() → BLE 扫描指定品牌设备
    │
    ▼
扫描完成 → ConfigWebPortal::begin()
    │
    ▼
用户连接 WiFi "无用脑洞研究所" → 192.168.4.1
    │
    ▼
登录 → 扫描 BMS 列表 → 选择配对（可选）
    │
    ▼
上传 .bmsui 文件 → HTTP POST /api/ui/upload
    │
    ▼
UiPackageStore::beginStaging() → 写入 /ui.tmp
    │
    ▼
serviceDeferredUiCommit() → UiPackageStore::commitStaging()
    ├── 校验 /ui.tmp（BMSUI 解析）
    ├── 备份 /ui.bmsui → /ui.bak
    ├── 复制 /ui.tmp → /ui.bmsui
    ├── SPIFFS remount + 再次校验
    └── 重启
    │
    ▼
DisplayUi::begin() → DynamicUi::begin()
    ├── 挂载 SPIFFS
    ├── 检查 PSRAM（硬门槛，≥1 MB）
    ├── 检查崩溃保护守卫
    ├── 加载 /ui.bmsui
    ├── buildScreen() 创建 LVGL 控件树
    └── 渲染首帧 → 清除启动守卫
```

---

## 8. 关键设计决策

| 决策 | 理由 |
|---|---|
| PSRAM 是自定义 UI 的硬门槛 | 动态 UI 控件树 + 图片资源需要大量堆内存，内部 RAM 320 KB 不足以承载复杂主题 |
| BLE 通知用环形缓冲区 | 解耦 NimBLE 回调（ISR 上下文）与主循环处理，避免回调中实时解析 |
| 主题上传两阶段（staging → commit → 重启） | 确保上传中断/网络断开不会破坏当前生效主题 |
| GATT 属性缓存 | 重连时跳过服务发现，加速重连（`useCachedAttributes`） |
| 配置文件用 Preferences (NVS) | 键值存储，掉电安全，无需文件系统 |
| 电流滤波 + 9 秒采样窗口判断车辆活动 | 避免电机启停瞬间的电流尖峰导致屏幕频繁亮灭 |
| 主循环 yield 2ms | 在 ESP32-S3 上平衡 BLE / HTTP / LVGL 刷新速度 |