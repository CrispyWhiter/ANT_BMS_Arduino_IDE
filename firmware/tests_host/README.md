# 主机测试

这些测试不需要 ESP32-S3 或真实保护板，用于验证可脱离硬件运行的核心逻辑。

覆盖：

- ANT 状态帧、电流方向和功率符号。
- JK02 24S/32S 组帧和字段映射。
- JBD 0x03/0x04/0x05 命令、校验和字段映射。
- 彦阳 Modbus RTU 请求、CRC 和寄存器映射。
- 仪表盘状态、测试模式和显示电源逻辑。
- BMSUI v1/v2/v3/v4/v5 解析、资源 CRC、VSHAPE、SOC-only 进度规则和兼容性。
- BmsData 到 UI 数值/状态绑定。

推荐从仓库根目录运行：

```bash
bash tools/run_host_tests.sh
```

主机测试不能替代 Arduino IDE 完整编译、ESP32-S3 烧录和真实 BLE/屏幕验收。
