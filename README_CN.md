# Multi-BMS ESP32-S3 Display

**版本：v1.0.0**  
**开发者：无用脑洞研究所**

## 1. 项目说明

本项目使用 ESP32-S3 + ST7789 320×240 横屏读取并显示四类保护板数据，支持蚂蚁 ANT、极空 JK、嘉佰达 JBD 和彦阳 YY。固件只读取状态，不写入保护参数或 MOS 控制命令。

主要功能：
- BLE 扫描、配对、自动重连与协议状态读取；
- 主仪表盘与单体电压分页；
- 剩余里程估算；
- 停车自动调暗/休眠与三击手动开关屏幕；
- 手机连接配置热点后，通过 Web 选择品牌、保护板、里程系数和休眠时间；
- 离线仪表盘测试模式。

## 2. 硬件与引脚

- MCU：ESP32-S3
- 屏幕：ST7789，320×240
- TFT CS：GPIO10
- TFT RST：GPIO9
- TFT DC：GPIO8
- TFT MOSI：GPIO11
- TFT SCLK：GPIO12
- TFT MISO：GPIO13
- 背光：GPIO7
- 用户按键：GPIO6

屏幕时序、旋转、RGB/BGR、背光 PWM 等参数统一位于 `src/app_config.h`。

## 3. Arduino IDE

打开 `ANT_BMS_Arduino_IDE.ino`。

建议：
- Board：ESP32S3 Dev Module
- Flash：至少 4 MB
- Partition Scheme：3 MB APP；工程同时提供 `partitions.csv`
- Core Debug Level：None
- 串口：115200 baud

依赖：
- Arduino-ESP32
- NimBLE-Arduino 2.x
- LVGL 8.4.x
- LovyanGFX

必须确保 LVGL 实际读取工程提供的 `lv_conf.h`。常见做法是把该文件放到 Arduino `libraries` 目录、与 `lvgl` 文件夹同级，然后重启 Arduino IDE 再编译。

## 4. Web 配置

配置热点名称固定为：

`无用脑洞研究所`

配置地址：

`http://192.168.4.1`

初始管理密码：

`123456`

登录成功后会直接进入设置页面，首次进入不会强制修改密码。页面采用紧凑深色卡片布局：先选保护板品牌，需要时重新扫描；从扫描列表选设备；设置里程系数；“保存并连接保护板”按钮位于设备选择状态正下方；屏幕休眠和管理密码位于同一页面下部。

测试模式开关仅在具备管理权限的登录会话中由服务端输出；普通登录不会收到测试功能页面内容，直接访问测试 API 也会被拒绝。管理专用凭据不在 Web 页面、串口日志、提示文字或文档中明文展示。切换测试模式后设备会自动重启；测试模式运行时 BLE 数据链路暂停，但配置热点保持可用，以便再次进入 Web 关闭测试模式。

品牌切换后必须执行对应品牌重新扫描。扫描期间 BLE 与 Wi-Fi 配置热点采用互斥流程，减少射频争用。保存保护板后设备会重启并进入正常 BLE 连接。

## 5. 支持协议

- ANT：常用 FFE0/FFE1 私有 BLE 数据通道。
- JK：JK02 常见 300 字节协议，支持 24S/32S 布局识别。
- JBD：FF00 服务，FF02 写入，FF01 通知；读取 0x03/0x04/0x05。
- YY：Modbus RTU over BLE，优先自动发现 FFE0/FF00 写入和通知特征，并提供 Nordic UART 后备。

不同厂商、年份或定制固件可能存在协议差异。若连接成功但没有有效数据，应保留从 BLE 启动开始的完整串口日志，并核对保护板型号、广播名、服务 UUID、从站地址和官方 App 数据。

## 6. 屏幕与休眠

屏幕参数集中在：
- `src/presentation/dashboard_ui_config.h`
- `src/presentation/dashboard_test_mode.h`

自动休眠默认开启，休眠时间可在 Web 设置为 5–10 分钟；变暗时间自动按“休眠时间减 3 分钟”计算。三击按键可手动休眠/唤醒；长按用于清除保护板配置并重新进入扫描流程。

应用层统一电流方向为：
- 正值：充电
- 负值：放电

## 7. 代码结构

- `src/application.*`：顶层初始化与协作式主循环
- `src/app_config.h`：固件身份、引脚和所有可调参数
- `src/services/bms_ble_service.*`：BLE 状态机、扫描、连接、GATT 与协议分发
- `src/services/config_web_portal.*`：配置热点、登录、Web UI 与 API
- `src/protocol/`：四类保护板协议解析
- `src/core/`：运行设置、数据仓库、日志与时间工具
- `src/presentation/`：仪表盘显示逻辑与样式配置
- `src/power/`：自动调暗/休眠逻辑
- `src/ui/`：LVGL 最小对象树、字体和线框资源
- `tests_host/`：可在电脑上编译运行的核心逻辑测试

## 8. 验证

`tests_host` 中包含 ANT、JK、JBD、YY、仪表盘状态、测试模式和自动休眠逻辑测试。发布前仍应在 Arduino IDE 中执行完整编译，并在目标 ESP32-S3、实际屏幕和对应保护板上完成 BLE、重连、Web 配置与长时间运行验收。
