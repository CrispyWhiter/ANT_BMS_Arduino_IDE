

#include "button_controller.h"

#include "../core/time_utils.h"

ButtonController::ButtonController(int pin,
                                   uint8_t activeLevel,
                                   uint32_t debounceMs,
                                   uint32_t multiClickWindowMs,
                                   uint8_t requiredMultiClickCount,
                                   uint32_t longPressMs,
                                   Callback singleClickCallback,
                                   Callback multiClickCallback,
                                   Callback longPressCallback)
    : pin_(pin),
      activeLevel_(activeLevel),
      debounceMs_(debounceMs),
      multiClickWindowMs_(multiClickWindowMs),
      requiredMultiClickCount_(requiredMultiClickCount),
      longPressMs_(longPressMs),
      singleClickCallback_(singleClickCallback),
      multiClickCallback_(multiClickCallback),
      longPressCallback_(longPressCallback) {}

void ButtonController::begin() {

  pinMode(pin_, activeLevel_ == LOW ? INPUT_PULLUP : INPUT_PULLDOWN);

  rawPressed_ = digitalRead(pin_) == activeLevel_;
  stablePressed_ = rawPressed_;
  debounceStartedAt_ = millis();
  pressedAt_ = 0;
  pressActive_ = false;
  longPressFired_ = false;
  clickCount_ = 0;
  clickDeadlineAt_ = 0;
}

void ButtonController::registerClick(uint32_t now) {

  if (clickCount_ == 0U ||
      TimeUtils::deadlineReached(now, clickDeadlineAt_)) {
    clickCount_ = 1U;
    clickDeadlineAt_ = TimeUtils::deadlineAfter(now, multiClickWindowMs_);
  } else if (clickCount_ < 0xFFU) {
    ++clickCount_;
  }

  if (requiredMultiClickCount_ > 0U &&
      clickCount_ >= requiredMultiClickCount_ &&
      !TimeUtils::deadlineReached(now, clickDeadlineAt_)) {
    clickCount_ = 0;
    clickDeadlineAt_ = 0;
    if (multiClickCallback_ != nullptr) multiClickCallback_();
  }
}

void ButtonController::dispatchPendingClicks() {
  if (clickCount_ == 0U) return;

  clickCount_ = 0;
  clickDeadlineAt_ = 0;
  if (singleClickCallback_ != nullptr) singleClickCallback_();
}

void ButtonController::loop() {
  const uint32_t now = millis();
  const bool currentRawPressed = digitalRead(pin_) == activeLevel_;

  if (currentRawPressed != rawPressed_) {
    rawPressed_ = currentRawPressed;
    debounceStartedAt_ = now;
  }

  if (TimeUtils::elapsedSince(now, debounceStartedAt_) >= debounceMs_ &&
      stablePressed_ != rawPressed_) {
    stablePressed_ = rawPressed_;

    if (stablePressed_) {

      pressedAt_ = now;
      pressActive_ = true;
      longPressFired_ = false;
    } else if (pressActive_) {

      pressActive_ = false;
      if (!longPressFired_) registerClick(now);
      pressedAt_ = 0;
    }
  }

  if (pressActive_ && !longPressFired_ &&
      TimeUtils::elapsedSince(now, pressedAt_) >= longPressMs_) {
    longPressFired_ = true;
    clickCount_ = 0;
    clickDeadlineAt_ = 0;
    if (longPressCallback_ != nullptr) longPressCallback_();
  }

  if (!pressActive_ && clickCount_ > 0U &&
      TimeUtils::deadlineReached(now, clickDeadlineAt_)) {
    dispatchPendingClicks();
  }
}
