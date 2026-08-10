#pragma once

#include <stdint.h>

#include "../bms_model.h"

namespace DashboardTestMode {

// Enable only for offline UI testing.

constexpr bool Enabled = false;

constexpr bool ShowBluetoothIcon = true;

constexpr bool EnableAutoDisplayPowerTest = false;

constexpr bool AnimateSoc = true;

constexpr uint8_t FixedSocPercent = 75;

constexpr uint8_t AnimationMinSoc = 0;

constexpr uint8_t AnimationMaxSoc = 100;

constexpr uint32_t AnimationStepIntervalMs = 50;

constexpr bool SimulateCharging = true;

constexpr float CurrentMagnitudeA = 12.5f;

constexpr float PowerMagnitudeW = 667.5f;

constexpr float RemainingRangeKm = 128.5f;

constexpr float RemainingCapacityAh = 35.0f;

constexpr float TotalCapacityAh = 70.0f;

constexpr float TotalVoltageV = 52.6f;

constexpr int16_t MosTemperatureC = 28;

constexpr float DeltaCellVoltageV = 0.007f;

constexpr uint16_t CycleCount = 5;

constexpr uint8_t CellCount = 20;

constexpr float AverageCellVoltageV = 3.700f;

constexpr float CellVoltageStepV = 0.001f;

uint8_t socPercent(uint32_t nowMs);

float currentA();

float powerW();

BmsData makeData(uint32_t nowMs);

}
