#pragma once

#include <Arduino.h>

namespace TimeUtils {

inline bool deadlineReached(uint32_t now, uint32_t deadline) {
  return deadline != 0U && static_cast<int32_t>(now - deadline) >= 0;
}

inline uint32_t elapsedSince(uint32_t now, uint32_t startedAt) {
  return now - startedAt;
}

inline uint32_t deadlineAfter(uint32_t now, uint32_t delayMs) {
  const uint32_t deadline = now + delayMs;

  return deadline == 0U ? 1U : deadline;
}

}
