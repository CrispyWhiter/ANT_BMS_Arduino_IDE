#pragma once

#include <Arduino.h>
#include <math.h>

#include "app_config.h"

struct BmsData {

  bool valid = false;
  uint32_t updatedAt = 0;

  uint8_t permissions = 0;
  uint8_t batteryStatus = 0;
  uint8_t temperatureCount = 0;
  uint8_t cellCount = 0;

  float cells[AppConfig::Protocol::MaxCellCount] = {};
  int16_t temperatures[AppConfig::Protocol::MaxTemperatureCount] = {};
  int16_t mosTemperature = 0;
  int16_t balancerTemperature = 0;

  float totalVoltage = 0.0f;
  float current = 0.0f;
  int32_t power = 0;
  uint16_t soc = 0;
  uint16_t soh = 0;

  uint8_t chargeMos = 0;
  uint8_t dischargeMos = 0;
  uint8_t balancerStatus = 0;

  float totalCapacityAh = 0.0f;
  float remainingCapacityAh = 0.0f;
  float cycleCapacityAh = 0.0f;
  uint32_t totalRuntimeSeconds = 0;
  uint32_t reportedCycleCount = 0;
  uint32_t balanceMask = 0;

  float maxCellVoltage = 0.0f;
  uint16_t maxCell = 0;
  float minCellVoltage = 0.0f;
  uint16_t minCell = 0;
  float deltaCellVoltage = 0.0f;
  float averageCellVoltage = 0.0f;

  uint16_t batteryType = 0;
  float totalDischargeAh = 0.0f;
  float totalChargeAh = 0.0f;
  uint32_t totalDischargeSeconds = 0;
  uint32_t totalChargeSeconds = 0;
  char hardwareVersion[17] = {};
  char softwareVersion[17] = {};

  float drivableRemainingCapacityAh() const {
    if (!isfinite(remainingCapacityAh) || remainingCapacityAh <= 0.0f) {
      return 0.0f;
    }

    const float usableCapacity =
        remainingCapacityAh - AppConfig::Range::ReserveCapacityAh;
    return usableCapacity > 0.0f ? usableCapacity : 0.0f;
  }

  float remainingRangeKm(float kilometersPerAmpHour) const {
    if (soc == 0U || !isfinite(kilometersPerAmpHour) ||
        kilometersPerAmpHour <= 0.0f) {
      return 0.0f;
    }
    return drivableRemainingCapacityAh() * kilometersPerAmpHour;
  }

  uint16_t cycleCount() const {
    if (reportedCycleCount > 0U) {
      return reportedCycleCount >= 65535U
                 ? 65535U
                 : static_cast<uint16_t>(reportedCycleCount);
    }

    if (!isfinite(totalCapacityAh) || !isfinite(cycleCapacityAh) ||
        totalCapacityAh <= 0.1f || cycleCapacityAh <= 0.0f) {
      return 0;
    }

    const float completedCycles = floorf(cycleCapacityAh / totalCapacityAh);
    if (completedCycles <= 0.0f) return 0;
    if (completedCycles >= 65535.0f) return 65535U;
    return static_cast<uint16_t>(completedCycles);
  }

};
