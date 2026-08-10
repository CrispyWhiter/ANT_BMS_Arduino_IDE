#include "runtime_settings.h"

#include <Preferences.h>
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../app_config.h"

namespace RuntimeSettings {
namespace {

constexpr char kNamespace[] = "ant_bms";
constexpr char kRangeKey[] = "range_km_ah";
constexpr char kPasswordKey[] = "web_pass";
constexpr char kDashboardTestKey[] = "dash_test";
constexpr char kConfigurationScanKey[] = "scan_next";
constexpr char kConfigurationScanTypeKey[] = "scan_type";
constexpr char kDisplayPowerKey[] = "disp_cfg";
constexpr uint32_t kDisplayPowerMagic = 0xA5000000UL;

float rangeCoefficient = AppConfig::Range::DefaultKilometersPerAmpHour;
char password[AppConfig::WebConfig::PasswordMaxLength + 1U] = {};
bool dashboardTestEnabled = false;
BmsType preferredScanType = BmsType::Ant;
DisplayPowerSettings displaySettings;
bool initialized = false;

bool isValidRange(float value) {
  return isfinite(value) &&
         value >= AppConfig::Range::MinimumKilometersPerAmpHour &&
         value <= AppConfig::Range::MaximumKilometersPerAmpHour;
}

void loadDefaults() {
  rangeCoefficient = AppConfig::Range::DefaultKilometersPerAmpHour;
  snprintf(password, sizeof(password), "%s", AppConfig::WebConfig::DefaultPassword);
  dashboardTestEnabled = false;
  preferredScanType = BmsType::Ant;
  displaySettings.enabled = AppConfig::DisplayPower::DefaultEnabled;
  displaySettings.sleepMinutes = AppConfig::DisplayPower::DefaultSleepMinutes;
}

}

bool isValidWebPassword(const char *candidate) {
  if (candidate == nullptr) return false;
  const size_t length = strlen(candidate);
  if (length < AppConfig::WebConfig::PasswordMinLength ||
      length > AppConfig::WebConfig::PasswordMaxLength) return false;
  for (size_t i = 0; i < length; ++i) {
    if (!isalnum(static_cast<unsigned char>(candidate[i]))) return false;
  }
  return true;
}

bool begin() {
  if (initialized) return true;
  loadDefaults();

  Preferences preferences;
  if (preferences.begin(kNamespace, true)) {
    const float storedRange = preferences.getFloat(
        kRangeKey, AppConfig::Range::DefaultKilometersPerAmpHour);
    if (isValidRange(storedRange)) rangeCoefficient = storedRange;

    char storedPassword[sizeof(password)] = {};
    preferences.getString(kPasswordKey, storedPassword, sizeof(storedPassword));
    if (isValidWebPassword(storedPassword)) {
      snprintf(password, sizeof(password), "%s", storedPassword);
    }

    dashboardTestEnabled = preferences.getBool(kDashboardTestKey, false);

    const uint8_t rawType = preferences.getUChar(
        kConfigurationScanTypeKey, static_cast<uint8_t>(BmsType::Ant));
    if (BmsTypeInfo::isValidRaw(rawType)) {
      preferredScanType = static_cast<BmsType>(rawType);
    }

    const uint32_t packedDisplay = preferences.getUInt(kDisplayPowerKey, 0U);
    if ((packedDisplay & 0xFF000000UL) == kDisplayPowerMagic) {
      const bool enabled = (packedDisplay & 0x00010000UL) != 0U;
      const uint8_t sleepMinutes = static_cast<uint8_t>(packedDisplay & 0xFFU);

      if (isValidDisplayPowerSettings(sleepMinutes)) {
        displaySettings.enabled = enabled;
        displaySettings.sleepMinutes = sleepMinutes;
      }
    }
    preferences.end();
  }

  initialized = true;
  return true;
}

float rangeCoefficientKmPerAh() {
  if (!initialized) begin();
  return rangeCoefficient;
}

bool setRangeCoefficientKmPerAh(float value) {
  if (!isValidRange(value)) return false;
  if (!initialized) begin();
  Preferences preferences;
  if (!preferences.begin(kNamespace, false)) return false;
  const size_t written = preferences.putFloat(kRangeKey, value);
  preferences.end();
  if (written == 0U) return false;
  rangeCoefficient = value;
  return true;
}

const char *webPassword() {
  if (!initialized) begin();
  return password;
}

bool setWebPassword(const char *candidate) {
  if (!isValidWebPassword(candidate)) return false;
  if (!initialized) begin();
  Preferences preferences;
  if (!preferences.begin(kNamespace, false)) return false;
  const size_t written = preferences.putString(kPasswordKey, candidate);
  preferences.end();
  if (written == 0U) return false;
  snprintf(password, sizeof(password), "%s", candidate);
  return true;
}

bool dashboardTestModeEnabled() {
  if (!initialized) begin();
  return dashboardTestEnabled;
}

bool setDashboardTestModeEnabled(bool enabled) {
  if (!initialized) begin();
  Preferences preferences;
  if (!preferences.begin(kNamespace, false)) return false;
  const size_t written = preferences.putBool(kDashboardTestKey, enabled);
  preferences.end();
  if (written == 0U) return false;
  dashboardTestEnabled = enabled;
  return true;
}

uint8_t DisplayPowerSettings::dimMinutes() const {
  return AppConfig::DisplayPower::derivedDimMinutes(sleepMinutes);
}

DisplayPowerSettings displayPowerSettings() {
  if (!initialized) begin();
  return displaySettings;
}

bool isValidDisplayPowerSettings(uint8_t sleepMinutes) {
  return sleepMinutes >= AppConfig::DisplayPower::MinimumSleepMinutes &&
         sleepMinutes <= AppConfig::DisplayPower::MaximumSleepMinutes;
}

bool setDisplayPowerSettings(bool enabled, uint8_t sleepMinutes) {
  if (!isValidDisplayPowerSettings(sleepMinutes)) return false;
  if (!initialized) begin();

  const uint32_t packed = kDisplayPowerMagic |
      (enabled ? 0x00010000UL : 0U) |
      static_cast<uint32_t>(sleepMinutes);

  Preferences preferences;
  if (!preferences.begin(kNamespace, false)) return false;
  const size_t written = preferences.putUInt(kDisplayPowerKey, packed);
  preferences.end();
  if (written == 0U) return false;

  displaySettings.enabled = enabled;
  displaySettings.sleepMinutes = sleepMinutes;
  return true;
}

bool requestConfigurationScanOnNextBoot(BmsType scanType) {
  if (!initialized) begin();
  Preferences preferences;
  if (!preferences.begin(kNamespace, false)) return false;
  const size_t typeWritten = preferences.putUChar(
      kConfigurationScanTypeKey, static_cast<uint8_t>(scanType));
  const size_t flagWritten = preferences.putBool(kConfigurationScanKey, true);
  preferences.end();
  if (typeWritten == 0U || flagWritten == 0U) return false;
  preferredScanType = scanType;
  return true;
}

bool consumeConfigurationScanOnNextBoot(BmsType &scanType) {
  if (!initialized) begin();
  scanType = preferredScanType;

  Preferences preferences;
  if (!preferences.begin(kNamespace, false)) return false;
  const uint8_t rawType = preferences.getUChar(
      kConfigurationScanTypeKey, static_cast<uint8_t>(preferredScanType));
  if (BmsTypeInfo::isValidRaw(rawType)) {
    scanType = static_cast<BmsType>(rawType);
    preferredScanType = scanType;
  }

  const bool requested = preferences.getBool(kConfigurationScanKey, false);
  if (requested) preferences.remove(kConfigurationScanKey);
  preferences.end();
  return requested;
}

BmsType preferredConfigurationScanType() {
  if (!initialized) begin();
  return preferredScanType;
}

}
