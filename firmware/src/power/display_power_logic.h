#pragma once

#include <stddef.h>
#include <stdint.h>

namespace DisplayPowerLogic {

enum class VehicleActivity : uint8_t {
  Unknown,
  Stationary,
  Active,
};

enum class ScreenState : uint8_t {
  Bright,
  Dimmed,
  AutoSleeping,
  ManualSleeping,
};

VehicleActivity resolveActivityForLink(VehicleActivity detectedActivity,
                                       bool bmsLinkConnected);

struct ActivityThresholds {
  float immediateActiveCurrentA = -3.0f;
  float sustainedActiveCurrentA = -1.5f;
  float stableIdleMinimumCurrentA = -1.0f;
  float readyFluctuationA = 0.8f;
  float stableFluctuationA = 0.3f;
  float fluctuationMustReachCurrentA = -0.5f;
  float filterAlpha = 0.35f;
  uint32_t sampleWindowMs = 9000U;
  uint32_t dataFreshMs = 8000U;
};

class VehicleActivityDetector {
 public:
  explicit VehicleActivityDetector(const ActivityThresholds &thresholds);

  void reset();
  void observe(float currentA, uint32_t frameAtMs);
  VehicleActivity classify(uint32_t nowMs) const;
  bool hasFreshData(uint32_t nowMs) const;

 private:
  struct Sample {
    float currentA = 0.0f;
    uint32_t atMs = 0U;
  };

  static constexpr size_t kSampleCapacity = 8U;

  ActivityThresholds thresholds_;
  Sample samples_[kSampleCapacity] = {};
  size_t sampleCount_ = 0U;
  size_t nextSampleIndex_ = 0U;
  bool hasFilteredCurrent_ = false;
  float filteredCurrentA_ = 0.0f;
  float latestCurrentA_ = 0.0f;
  uint32_t latestFrameAtMs_ = 0U;
};

class DisplayPowerPolicy {
 public:
  void reset(uint32_t nowMs);
  void update(uint32_t nowMs,
              VehicleActivity activity,
              bool autoSleepEnabled,
              bool configurationClientConnected,
              uint32_t dimDelayMs,
              uint32_t sleepDelayMs);

  void toggleManual(uint32_t nowMs);
  void notifyUserInteraction(uint32_t nowMs);

  ScreenState state() const { return state_; }

 private:
  void restartStationaryTimer(uint32_t nowMs);

  ScreenState state_ = ScreenState::Bright;
  bool stationaryTiming_ = false;
  uint32_t stationarySinceMs_ = 0U;
};

}
