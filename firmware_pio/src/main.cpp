// PlatformIO 入口 —— 对应原 Arduino sketch firmware/firmware.ino。
// 主循环栈仍按原工程设置为 20480 bytes（覆盖 Web 配置和主题上传调用路径）。
#include <Arduino.h>

#if defined(SET_LOOP_TASK_STACK_SIZE)
SET_LOOP_TASK_STACK_SIZE(20480);
#else
size_t getArduinoLoopTaskStackSize() { return 20480; }
#endif

#include "application.h"

void setup() {
  Application::begin();
}

void loop() {
  Application::loop();
}
