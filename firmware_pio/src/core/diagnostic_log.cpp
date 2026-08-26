#include "diagnostic_log.h"

#include <Arduino.h>
#include <stdarg.h>

namespace DiagnosticLog {
namespace {

SemaphoreHandle_t serialMutex = nullptr;

class ScopedLock {
 public:
  explicit ScopedLock(TickType_t timeout)
      : locked_(serialMutex != nullptr &&
                xSemaphoreTake(serialMutex, timeout) == pdTRUE) {}

  ~ScopedLock() {
    if (locked_) xSemaphoreGive(serialMutex);
  }

  bool locked() const { return locked_; }

 private:
  bool locked_;
};

}

bool begin() {
  if (serialMutex != nullptr) return true;
  serialMutex = xSemaphoreCreateMutex();
  return serialMutex != nullptr;
}

void write(const char *text) {
  if (text == nullptr) return;

  if (serialMutex == nullptr) {
    Serial.print(text);
    return;
  }

  ScopedLock lock(pdMS_TO_TICKS(10));
  if (lock.locked()) Serial.print(text);
}

void printf(const char *format, ...) {
  if (format == nullptr) return;

  char buffer[384];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  write(buffer);
}

}
