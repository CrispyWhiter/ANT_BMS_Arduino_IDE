# ESP32-S3 BMS 显示固件

**版本：v1.1.0**

入口：`ANT_BMS_Arduino_IDE.ino`

固件读取 ANT、JK、JBD、彦阳 BMS 状态并显示到 ST7789 320×240 屏幕，同时提供配置热点和 BMSUI v1–v7 动态主题 runtime。固件只读取状态，不发送保护参数修改或 MOS 控制命令。

## 结构

```text
src/application.*                 初始化与主循环
src/app_config.h                  集中运行参数
src/core/                         数据、设置、日志、时间
src/protocol/                     ANT/JK/JBD/YY 协议
src/services/bms_ble_service.*    BLE 状态机
src/services/config_web_portal.*  Web 配置和 UI API
src/presentation/                 Factory UI
src/ui_runtime/                   BMSUI parser、存储、动态 UI
src/power/                        屏幕电源逻辑
src/ui/                           LVGL 内置资源
tests_host/                       主机纯逻辑测试
```

硬件、依赖、PSRAM、分区和部署说明见 [`../docs/固件配置与部署.md`](../docs/固件配置与部署.md)。
