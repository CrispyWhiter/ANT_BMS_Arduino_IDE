#include <Arduino.h>

#if defined(SET_LOOP_TASK_STACK_SIZE)
SET_LOOP_TASK_STACK_SIZE(20480);
#else
size_t getArduinoLoopTaskStackSize() { return 20480; }
#endif

#include "src/application.h"

void setup() {
  Application::begin();
}

void loop() {
  Application::loop();
}
