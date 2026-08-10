#include "display_power_logic.h"

#include <math.h>

namespace DisplayPowerLogic {
namespace {

bool elapsedAtLeast(uint32_t nowMs, uint32_t startedAtMs, uint32_t durationMs) {
  return static_cast<uint32_t>(nowMs - startedAtMs) >= durationMs;
}

bool finiteCurrent(float value) {
  return isfinite(value) != 0;
}

}

VehicleActivity resolveActivityForLink(VehicleActivity detectedActivity,
                                       bool bmsLinkConnected) {

  if (!bmsLinkConnected) return VehicleActivity::Stationary;
  return detectedActivity;
}

VehicleActivityDetector::VehicleActivityDetector(
    const ActivityThresholds &thresholds)
    : thresholds_(thresholds) {}

void VehicleActivityDetector::reset() {
  sampleCount_ = 0U;
  nextSampleIndex_ = 0U;
  hasFilteredCurrent_ = false;
  filteredCurrentA_ = 0.0f;
  latestCurrentA_ = 0.0f;
  latestFrameAtMs_ = 0U;
  for (size_t i = 0U; i < kSampleCapacity; ++i) samples_[i] = Sample{};
}

void VehicleActivityDetector::observe(float currentA, uint32_t frameAtMs) {
  if (!finiteCurrent(currentA)) return;

  if (sampleCount_ > 0U && frameAtMs == latestFrameAtMs_) {
    latestCurrentA_ = currentA;
    return;
  }

  latestCurrentA_ = currentA;
  latestFrameAtMs_ = frameAtMs;

  if (!hasFilteredCurrent_) {
    filteredCurrentA_ = currentA;
    hasFilteredCurrent_ = true;
  } else {
    float alpha = thresholds_.filterAlpha;
    if (alpha < 0.0f) alpha = 0.0f;
    if (alpha > 1.0f) alpha = 1.0f;
    filteredCurrentA_ += alpha * (currentA - filteredCurrentA_);
  }

  samples_[nextSampleIndex_].currentA = currentA;
  samples_[nextSampleIndex_].atMs = frameAtMs;
  nextSampleIndex_ = (nextSampleIndex_ + 1U) % kSampleCapacity;
  if (sampleCount_ < kSampleCapacity) ++sampleCount_;
}

bool VehicleActivityDetector::hasFreshData(uint32_t nowMs) const {
  if (sampleCount_ == 0U) return false;
  return static_cast<uint32_t>(nowMs - latestFrameAtMs_) <=
         thresholds_.dataFreshMs;
}

VehicleActivity VehicleActivityDetector::classify(uint32_t nowMs) const {
  if (!hasFreshData(nowMs) || !hasFilteredCurrent_) {
    return VehicleActivity::Unknown;
  }

  if (latestCurrentA_ <= thresholds_.immediateActiveCurrentA ||
      filteredCurrentA_ <= thresholds_.sustainedActiveCurrentA) {
    return VehicleActivity::Active;
  }

  bool found = false;
  size_t recentCount = 0U;
  float minimum = 0.0f;
  float maximum = 0.0f;
  for (size_t i = 0U; i < sampleCount_; ++i) {
    const Sample &sample = samples_[i];
    if (static_cast<uint32_t>(nowMs - sample.atMs) >
        thresholds_.sampleWindowMs) {
      continue;
    }
    ++recentCount;
    if (!found) {
      minimum = maximum = sample.currentA;
      found = true;
    } else {
      if (sample.currentA < minimum) minimum = sample.currentA;
      if (sample.currentA > maximum) maximum = sample.currentA;
    }
  }

  const float range = found ? maximum - minimum : 0.0f;
  if (recentCount >= 2U &&
      range >= thresholds_.readyFluctuationA &&
      minimum <= thresholds_.fluctuationMustReachCurrentA) {
    return VehicleActivity::Active;
  }

  if (latestCurrentA_ >= 0.0f) return VehicleActivity::Stationary;

  if (latestCurrentA_ >= thresholds_.stableIdleMinimumCurrentA) {
    if (recentCount < 2U || range <= thresholds_.stableFluctuationA) {
      return VehicleActivity::Stationary;
    }
  }

  return VehicleActivity::Unknown;
}

void DisplayPowerPolicy::reset(uint32_t nowMs) {
  state_ = ScreenState::Bright;
  stationaryTiming_ = false;
  stationarySinceMs_ = nowMs;
}

void DisplayPowerPolicy::restartStationaryTimer(uint32_t nowMs) {
  stationaryTiming_ = false;
  stationarySinceMs_ = nowMs;
}

void DisplayPowerPolicy::update(uint32_t nowMs,
                                VehicleActivity activity,
                                bool autoSleepEnabled,
                                bool configurationClientConnected,
                                uint32_t dimDelayMs,
                                uint32_t sleepDelayMs) {
  if (state_ == ScreenState::ManualSleeping) return;

  if (!autoSleepEnabled || configurationClientConnected) {
    state_ = ScreenState::Bright;
    restartStationaryTimer(nowMs);
    return;
  }

  if (activity == VehicleActivity::Active) {
    state_ = ScreenState::Bright;
    restartStationaryTimer(nowMs);
    return;
  }

  if (activity == VehicleActivity::Unknown) {

    restartStationaryTimer(nowMs);
    return;
  }

  if (!stationaryTiming_) {
    stationaryTiming_ = true;
    stationarySinceMs_ = nowMs;
  }

  if (sleepDelayMs > dimDelayMs &&
      elapsedAtLeast(nowMs, stationarySinceMs_, sleepDelayMs)) {
    state_ = ScreenState::AutoSleeping;
  } else if (elapsedAtLeast(nowMs, stationarySinceMs_, dimDelayMs)) {
    state_ = ScreenState::Dimmed;
  } else {
    state_ = ScreenState::Bright;
  }
}

void DisplayPowerPolicy::toggleManual(uint32_t nowMs) {
  if (state_ == ScreenState::ManualSleeping ||
      state_ == ScreenState::AutoSleeping) {
    state_ = ScreenState::Bright;
    restartStationaryTimer(nowMs);
    return;
  }

  state_ = ScreenState::ManualSleeping;
  restartStationaryTimer(nowMs);
}

void DisplayPowerPolicy::notifyUserInteraction(uint32_t nowMs) {
  if (state_ == ScreenState::ManualSleeping) return;
  state_ = ScreenState::Bright;
  restartStationaryTimer(nowMs);
}

}
