#pragma once

#include <Arduino.h>

#include "../bms_type.h"

namespace RuntimeSettings {

struct DisplayPowerSettings {
  bool enabled = true;
  uint8_t sleepMinutes = 5;

  uint8_t dimMinutes() const;
};

bool begin();
float rangeCoefficientKmPerAh();
bool setRangeCoefficientKmPerAh(float value);
const char *webPassword();
bool setWebPassword(const char *password);
bool isValidWebPassword(const char *password);

bool dashboardTestModeEnabled();
bool setDashboardTestModeEnabled(bool enabled);

DisplayPowerSettings displayPowerSettings();
bool isValidDisplayPowerSettings(uint8_t sleepMinutes);
bool setDisplayPowerSettings(bool enabled, uint8_t sleepMinutes);

bool requestConfigurationScanOnNextBoot(BmsType scanType);
bool consumeConfigurationScanOnNextBoot(BmsType &scanType);
BmsType preferredConfigurationScanType();

}
