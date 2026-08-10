v1.0.0 主机测试
================

这些测试不需要 ESP32 或真实保护板，用于验证核心纯逻辑：
- ANT 状态帧组帧、电流方向归一化与功率符号；
- JK02 24S/32S 组帧、字段映射和布局纠正；
- JBD 0x03/0x04/0x05 命令、分包、校验和字段映射；
- 彦阳 Modbus RTU 请求、CRC、分包和寄存器映射；
- 仪表盘状态映射、充电判定与闪烁相位；
- 自动调暗/息屏逻辑。

Linux/macOS 示例：
  cd tests_host
  g++ -std=c++17 -Wall -Wextra -Werror -Istubs -I../src \
      test_ant_protocol.cpp ../src/protocol/ant_protocol.cpp -o test_ant_protocol
  ./test_ant_protocol

其余测试按相同方式编译。主机测试不能替代 Arduino IDE 全量编译、ESP32-S3 烧录和真实 BLE/屏幕验收。
