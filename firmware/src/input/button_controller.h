#pragma once

#include <Arduino.h>

class ButtonController {
 public:
  using Callback = void (*)();

  ButtonController(int pin,
                   uint8_t activeLevel,
                   uint32_t debounceMs,
                   uint32_t multiClickWindowMs,
                   uint8_t requiredMultiClickCount,
                   uint32_t longPressMs,
                   Callback singleClickCallback,
                   Callback multiClickCallback,
                   Callback longPressCallback);

  void begin();

  void loop();

 private:
  void registerClick(uint32_t now);
  void dispatchPendingClicks();

  const int pin_;
  const uint8_t activeLevel_;
  const uint32_t debounceMs_;
  const uint32_t multiClickWindowMs_;
  const uint8_t requiredMultiClickCount_;
  const uint32_t longPressMs_;
  const Callback singleClickCallback_;
  const Callback multiClickCallback_;
  const Callback longPressCallback_;

  bool rawPressed_ = false;
  bool stablePressed_ = false;
  bool pressActive_ = false;
  bool longPressFired_ = false;
  uint32_t debounceStartedAt_ = 0;
  uint32_t pressedAt_ = 0;
  uint8_t clickCount_ = 0;
  uint32_t clickDeadlineAt_ = 0;
};
