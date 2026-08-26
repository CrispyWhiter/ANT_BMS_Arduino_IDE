# This is a fork
将firmware部分迁移到PlatformIO

# BMS UI Designer + ESP32-S3 Dynamic UI

**版本：v1.1.0**  
**开发者：无用脑洞研究所**

面向 ESP32-S3 + ST7789 320×240 屏幕的 BMS 状态显示项目。固件支持 ANT、JK、JBD、彦阳四类保护板，Windows UI Designer 用于设计、预览、读取和下发动态界面。

v1.1.0 的设备主题协议为 **BMSUI v7**，固件继续兼容读取 v1–v6 主题。第一次从 v1.0.0 升级时需要通过 USB 烧录一次 v1.1.0 固件；之后仅修改主题时，可直接通过设备配置热点发送 UI。

## 功能概览

### Windows UI Designer

- 320×240 逻辑画布，大尺寸编辑区 + 1:1 实际尺寸预览。
- 拖动、八方向缩放、四边直接缩放、`Shift` 等比例缩放。
- 框选多选、`Ctrl+点击`、整组拖动、方向键微调、撤销/重做。
- 静态文字、动态数值、PNG/JPG、双状态图片、矩形/圆形/椭圆。
- SOC 圆环与条形进度；剩余容量、剩余里程条形进度。
- 颜色、透明度、圆角、边框、渐变、图层顺序。
- Windows 系统字体；动态数值通过 BMSUI v6+ 字形图集保持 PC/设备显示一致。
- BMSUI v7 自动优化图片格式、资源去重和 Base64 传输。
- `.esptheme` 本地工程保存；设备端 `.bmsui` 自动生成和下发。

### ESP32-S3 固件

- ANT / JK / JBD / 彦阳 BMS BLE 读取与统一数据模型。
- Web 配置热点、保护板扫描/选择、里程系数和屏幕休眠设置。
- BMSUI v1–v7 解析、SPIFFS 事务式主题更新和异常回退。
- 固件内置 Factory UI 永久保留，自定义主题失败时自动兜底。
- 图片、字体和动态 UI 大资源优先使用 PSRAM；8 MB / 16 MB PSRAM 均按运行时实际可用容量处理。
- 自动调暗、休眠、按键三击开关屏幕和长按进入配置模式。

## 硬件引脚

这部分我根据个人习惯修改过，和原项目有区别

| 功能 | GPIO |
|---|---:|
| TFT CS | 13 |
| TFT RST | 12 |
| TFT DC | 11 |
| TFT MOSI | 10 |
| TFT SCLK | 9 |
| TFT MISO | 3 |
| TFT 背光 | 7 | 默认高电平有效
| 用户按键 | 6 |

完整参数集中在 `firmware/src/app_config.h`。

## 屏幕参数

`firmware/src/display_ui.cpp`:
config.offset_rotation  控制屏幕旋转

`firmware/src/app_config.h`:
constexpr bool InvertColors 控制屏幕颜色翻转

其他参数自行按字面意思测试即可

## 快速开始

1. 用 Arduino IDE 打开 `firmware/ANT_BMS_Arduino_IDE.ino` 并烧录到目标 ESP32-S3。
2. 长按用户按键 10 秒，设备重启进入配置/UI 热点。
3. 电脑连接热点 `无用脑洞研究所`，设备地址为 `192.168.4.1`。
4. 运行 `release/BMS_UI_Designer_v1.1.0.exe`。
5. 输入管理密码，点击“检测设备”。
6. 设计主题后点击“发送 UI”。
7. 需要回退时点击“恢复原始 UI”。

默认管理密码为 `123456`，首次部署后建议立即修改。

## 仓库结构

```text
designer/              Windows UI Designer 源码
firmware/              ESP32-S3 Arduino 固件
examples/              BMSUI 兼容与功能示例
assets/                图标和参考主题资源
docs/                  使用、协议、升级、审计和发布说明
tools/                 主机测试、校验和设备调试工具
release/               Windows 发布文件
```

## 文档

- [固件配置与部署](docs/固件配置与部署.md)
- [UI Designer 使用说明](docs/UI编辑器使用说明.md)
- [BMSUI v7 协议](docs/BMSUI_PROTOCOL.md)
- [从 v1.0.0 升级到 v1.1.0](docs/从_v1.0.0_升级到_v1.1.0.md)
- [代码审计说明](docs/代码审计说明.md)
- [GitHub Release v1.1.0](docs/GitHub发布说明_v1.1.0.md)
- [发布检查清单](docs/发布检查清单.md)
- [更新日志](CHANGELOG.md)

## 构建与测试

主机侧完整检查：

```bash
bash tools/run_host_tests.sh
```

覆盖四类 BMS 协议、显示逻辑、BMSUI parser、UI 数据绑定、示例校验、Designer 源码检查、源码审计和 Win64 Go 交叉编译。

ESP32-S3 最终 Release 仍应在目标 Arduino 环境完成一次完整编译、烧录和实机回归。

## 发布说明

GitHub 上保留已有 `v1.0.0` Tag/Release，新建 `v1.1.0` Tag/Release，不覆盖历史版本。Release 附件和网页操作步骤见 `docs/发布检查清单.md`。
