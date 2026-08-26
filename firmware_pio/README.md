# firmware_pio —— PlatformIO 迁移工程

本目录是仓库 `firmware/`（Arduino IDE sketch）迁移到 **PlatformIO** 的独立工程，
固件代码与 `firmware/src` 完全一致（v1.1.0，ESP32-S3 + ST7789，ANT/JK/JBD/彦阳 BMS 显示）。

## 目录结构

```text
firmware_pio/
├── platformio.ini        构建配置（板型、分区、宏、依赖）
├── partitions.csv        4MB 分区表（3MB app + 1MB SPIFFS），与 firmware/ 同步
├── src/                  固件源码（原 firmware/src 原样复制）
│   ├── main.cpp          PlatformIO 入口（对应原 firmware.ino）
│   └── ...               与 firmware/src 相同的目录结构
└── README.md
```

> **lv_conf.h 的放置**：已直接放入 lvgl 库目录
> `.pio/libdeps/esp32s3/lvgl/lv_conf.h`（经 lvgl 的 `includeDir: "."` 及内部相对路径
> `../../lv_conf.h` 自动找到，无需 `-DLV_CONF_INCLUDE_SIMPLE`）。
> **自动同步**：`platformio.ini` 里的 `extra_scripts = scripts/ensure_lv_conf.py`
> 会在每次构建前自动把仓库权威版 `firmware/lv_conf.h` 复制到该位置，因此
> `pio pkg update` / 删除 `.pio` / 新机器首次构建后无需手工恢复。
> 如需修改 LVGL 配置，请改仓库 `firmware/lv_conf.h`（git 追踪）。

## 与原 Arduino 工程的对应关系

| 原 Arduino IDE 工程 | PlatformIO 工程 |
|---|---|
| `firmware/firmware.ino` | `src/main.cpp`（入口逻辑一致，含 20480 主循环栈） |
| `firmware/build_opt.h` | `platformio.ini` → `build_flags`（`-DLGFX_USE_LVGL` 等；`-DLV_CONF_INCLUDE_SIMPLE` 因 lv_conf.h 移入 lvgl 库目录而省略） |
| `firmware/lv_conf.h` | `lvgl` 库目录内的 `lv_conf.h`（`.pio/libdeps/esp32s3/lvgl/lv_conf.h`） |
| `firmware/partitions.csv` | `partitions.csv`（`board_build.partitions` 引用） |
| Arduino 库：Arduino-ESP32 3.x | `platform = espressif32@^6.9.0` |
| 库：NimBLE-Arduino 2.x | `lib_deps: h2zero/NimBLE-Arduino@^2.2.3` |
| 库：LVGL 8.4.x | `lib_deps: lvgl@^8.4.0` |
| 库：LovyanGFX | `lib_deps: lovyan03/LovyanGFX@^1.1.9` |

## 常用命令

```bash
# 编译
pio run

# 编译并烧录（USB 连接）
pio run -t upload

# 烧录 SPIFFS 文件系统镜像（预留，data/ 目录当前为空）
pio run -t uploadfs

# 串口监视（115200）
pio device monitor
```

## 硬件注意点

- **Flash**：`partitions.csv` 为 4MB 分区布局（3MB app + 1MB SPIFFS）。`board_build.flash_size`
  按实际芯片容量设置（当前 = 16MB），修改 flash 容量时保持分区表不变即可。
- **PSRAM（当前硬件 ESP32-S3-N16R8）**：已通过 `board_build.arduino.memory_type = qio_opi`
  （Flash QIO + 8MB Octal PSRAM）和 `-DBOARD_HAS_PSRAM` 启用。**自定义主题/动态 UI 强制要求
  PSRAM ≥ 1MB**（`DynamicUi::begin()` 无 PSRAM 时直接使用 Factory UI 并保留上传包）。
  若换用其他模组：Quad PSRAM 用 `qio_qspi`，无 PSRAM 则自定义主题不可用。
- **USB CDC**：默认 `-DARDUINO_USB_CDC_ON_BOOT=1`（Serial 走原生 USB，等效 Arduino IDE
  默认配置）。板子 Serial 走 UART 桥时改为 `0`。
- 接线 GPIO 定义见 `src/app_config.h`（TFT 与按键），与 `firmware/` 一致。

## 主机测试

`firmware/tests_host` 的主机纯逻辑测试（g++ 编译、无需硬件）未迁入本目录，仍从仓库根
运行：

```bash
bash tools/run_host_tests.sh
```

修改固件源码时请注意：`firmware/` 与 `firmware_pio/src/` 是两份副本，需要同步改动。
