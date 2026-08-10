#include "dashboard_test_mode.h"

#include <math.h>

#include "../app_config.h"

namespace DashboardTestMode {
namespace {

uint8_t clampSoc(int value) {
  if (value < 0) return 0U;
  if (value > 100) return 100U;
  return static_cast<uint8_t>(value);
}

}

uint8_t socPercent(uint32_t nowMs) {
  const uint8_t fixedSoc = clampSoc(static_cast<int>(FixedSocPercent));
  if (!AnimateSoc) return fixedSoc;

  const uint8_t minSoc = clampSoc(static_cast<int>(AnimationMinSoc));
  const uint8_t maxSoc = clampSoc(static_cast<int>(AnimationMaxSoc));
  if (maxSoc <= minSoc || AnimationStepIntervalMs == 0U) return minSoc;

  const uint32_t span = static_cast<uint32_t>(maxSoc - minSoc);
  const uint32_t roundTripSteps = span * 2U;
  const uint32_t step = (nowMs / AnimationStepIntervalMs) % roundTripSteps;

  if (step <= span) {
    return static_cast<uint8_t>(minSoc + step);
  }

  return static_cast<uint8_t>(maxSoc - (step - span));
}

float currentA() {
  const float magnitude = fabsf(CurrentMagnitudeA);
  return SimulateCharging ? magnitude : -magnitude;
}

float powerW() {
  const float magnitude = fabsf(PowerMagnitudeW);
  return SimulateCharging ? magnitude : -magnitude;
}

BmsData makeData(uint32_t nowMs) {
  BmsData data{};

  data.valid = true;
  data.updatedAt = nowMs;
  data.soc = socPercent(nowMs);
  data.soh = 100U;

  data.totalVoltage = TotalVoltageV;
  data.current = currentA();
  data.power = static_cast<int32_t>(lroundf(powerW()));
  data.totalCapacityAh = TotalCapacityAh;
  data.remainingCapacityAh = RemainingCapacityAh;
  data.mosTemperature = MosTemperatureC;
  data.reportedCycleCount = CycleCount;

  data.chargeMos = 1U;
  data.dischargeMos = 1U;
  data.balancerStatus = 0U;

  const uint8_t maximumCellCount = AppConfig::Protocol::MaxCellCount;
  data.cellCount = CellCount <= maximumCellCount ? CellCount : maximumCellCount;

  float minimumCell = 1000.0f;
  float maximumCell = 0.0f;
  float cellSum = 0.0f;
  uint16_t minimumCellIndex = 0U;
  uint16_t maximumCellIndex = 0U;

  for (uint8_t index = 0U; index < data.cellCount; ++index) {
    const int8_t pattern = static_cast<int8_t>(index % 7U) - 3;
    const float cellVoltage =
        AverageCellVoltageV + static_cast<float>(pattern) * CellVoltageStepV;

    data.cells[index] = cellVoltage;
    cellSum += cellVoltage;

    if (cellVoltage < minimumCell) {
      minimumCell = cellVoltage;
      minimumCellIndex = static_cast<uint16_t>(index + 1U);
    }

    if (cellVoltage > maximumCell) {
      maximumCell = cellVoltage;
      maximumCellIndex = static_cast<uint16_t>(index + 1U);
    }
  }

  if (data.cellCount > 0U) {
    data.minCellVoltage = minimumCell;
    data.maxCellVoltage = maximumCell;
    data.minCell = minimumCellIndex;
    data.maxCell = maximumCellIndex;
    data.averageCellVoltage = cellSum / static_cast<float>(data.cellCount);
  }

  data.deltaCellVoltage = DeltaCellVoltageV;

  data.temperatureCount = 3U;
  data.temperatures[0] = MosTemperatureC;
  data.temperatures[1] = static_cast<int16_t>(MosTemperatureC - 1);
  data.temperatures[2] = static_cast<int16_t>(MosTemperatureC + 1);

  return data;
}

}
