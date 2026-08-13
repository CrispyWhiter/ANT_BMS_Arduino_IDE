# BMS UI Designer v1.1.0

`BMS_UI_Designer_v1.1.0.exe` 为 Windows 64 位正式构建。

主要能力：

- 320×240 动态 UI 设计和 1:1 预览。
- `.esptheme` 工程文件。
- ESP32-S3 主题读取、发送和恢复 Factory UI。
- BMSUI v7 下发。
- 图片自动选择 A8 / RGB565 / RGB565A8，重复资源去重。
- 动态字体最小共享字形集。
- BMSUI v7 `ASSETB64` 资源传输。
- 根据设备 `/api/ui/info` 自动采用运行时资源上限。

从 v1.0.0 升级时，需要先通过 USB 烧录配套 v1.1.0 固件一次。
