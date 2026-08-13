#pragma once

#include <math.h>
#include <stdint.h>

#include "../bms_model.h"

namespace DashboardStatusLogic {

inline uint8_t clampSocPercent(uint16_t soc) {
  return soc > 100U ? 100U : static_cast<uint8_t>(soc);
}

inline uint8_t visibleSocCellCount(uint16_t soc, uint8_t totalCells) {
  if (totalCells == 0U) return 0U;

  const uint8_t clampedSoc = clampSocPercent(soc);
  if (clampedSoc == 0U) return 0U;

  const uint32_t scaled =
      static_cast<uint32_t>(clampedSoc) * static_cast<uint32_t>(totalCells);
  const uint32_t roundedUp = (scaled + 99U) / 100U;
  return roundedUp > totalCells ? totalCells
                                : static_cast<uint8_t>(roundedUp);
}

inline bool isLowSoc(uint16_t soc, uint8_t thresholdPercent) {
  const uint8_t threshold = thresholdPercent > 100U ? 100U : thresholdPercent;
  return clampSocPercent(soc) <= threshold;
}

inline bool isCharging(const BmsData &data, float currentThresholdA) {
  if (!data.valid || !isfinite(data.current)) return false;

  if (!isfinite(currentThresholdA) || currentThresholdA <= 0.0f) {
    return false;
  }
  return data.current >= currentThresholdA;
}

class ChargingStateDetector {
 public:
  void reset() { charging_ = false; }

  bool update(const BmsData &data,
              float enterThresholdA,
              float exitThresholdA) {
    if (!data.valid || !isfinite(data.current) ||
        !isfinite(enterThresholdA) || !isfinite(exitThresholdA) ||
        enterThresholdA <= 0.0f || exitThresholdA < 0.0f ||
        exitThresholdA >= enterThresholdA) {
      charging_ = false;
      return false;
    }

    if (charging_) {
      if (data.current <= exitThresholdA) charging_ = false;
    } else if (data.current >= enterThresholdA) {
      charging_ = true;
    }
    return charging_;
  }

  bool active() const { return charging_; }

 private:
  bool charging_ = false;
};

inline bool blinkVisible(uint32_t elapsedMs, uint32_t intervalMs) {
  if (intervalMs == 0U) return true;
  return ((elapsedMs / intervalMs) & 1U) == 0U;
}

}
