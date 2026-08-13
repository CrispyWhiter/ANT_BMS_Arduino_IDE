# 固件依赖

本项目按以下主版本 API 编写：

| 依赖 | 建议范围 | 用途 |
|---|---|---|
| Arduino-ESP32 | 3.x | ESP32-S3 Arduino Core、Wi-Fi、SPIFFS、heap capability |
| NimBLE-Arduino | 2.x | BMS BLE 扫描、连接、订阅和通知 |
| LVGL | 8.4.x | Factory UI 与动态 UI runtime |
| LovyanGFX | 与 Arduino-ESP32 兼容的稳定版 | ST7789 显示驱动 |

## 重要要求

- 必须使用工程提供的 `firmware/lv_conf.h`。
- `partitions.csv` 必须与 `ANT_BMS_Arduino_IDE.ino` 位于同一 sketch 目录。
- 不建议在发布前临时跨 LVGL 8 → 9 或 NimBLE 2 → 其他主版本；主版本升级应单独建分支回归。
- Release 前建议记录实际验证通过的 Arduino IDE、开发板包和库的精确版本，便于后续复现。
