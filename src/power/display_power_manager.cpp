#include "display_power_manager.h"

#include <Arduino.h>
#include <math.h>

#include "../app_config.h"
#include "../core/diagnostic_log.h"
#include "../core/runtime_settings.h"
#include "../display_ui.h"
#include "display_power_logic.h"

namespace DisplayPowerManager {
namespace {

using DisplayPowerLogic::ActivityThresholds;
using DisplayPowerLogic::DisplayPowerPolicy;
using DisplayPowerLogic::ScreenState;
using DisplayPowerLogic::VehicleActivity;
using DisplayPowerLogic::VehicleActivityDetector;

ActivityThresholds makeThresholds() {
  ActivityThresholds value;
  value.immediateActiveCurrentA =
      AppConfig::DisplayPower::ImmediateActiveCurrentA;
  value.sustainedActiveCurrentA =
      AppConfig::DisplayPower::SustainedActiveCurrentA;
  value.stableIdleMinimumCurrentA =
      AppConfig::DisplayPower::StableIdleMinimumCurrentA;
  value.readyFluctuationA = AppConfig::DisplayPower::ReadyFluctuationA;
  value.stableFluctuationA = AppConfig::DisplayPower::StableFluctuationA;
  value.fluctuationMustReachCurrentA =
      AppConfig::DisplayPower::FluctuationMustReachCurrentA;
  value.filterAlpha = AppConfig::DisplayPower::CurrentFilterAlpha;
  value.sampleWindowMs = AppConfig::DisplayPower::CurrentSampleWindowMs;
  value.dataFreshMs = AppConfig::DisplayPower::CurrentDataFreshMs;
  return value;
}

VehicleActivityDetector activityDetector(makeThresholds());
DisplayPowerPolicy policy;
ScreenState appliedState = ScreenState::Bright;
bool initialized = false;
bool previousBmsLinkConnected = false;

const char *stateName(ScreenState state) {
  switch (state) {
    case ScreenState::Bright: return "bright";
    case ScreenState::Dimmed: return "dimmed";
    case ScreenState::AutoSleeping: return "auto-sleep";
    case ScreenState::ManualSleeping: return "manual-sleep";
  }
  return "unknown";
}

void applyState(ScreenState state) {
  if (!initialized || state == appliedState) return;

  switch (state) {
    case ScreenState::Bright:
      DisplayUi::setBrightnessPercent(
          AppConfig::DisplayPower::NormalBrightnessPercent);
      DisplayUi::setEnabled(true);
      break;

    case ScreenState::Dimmed:
      DisplayUi::setBrightnessPercent(
          AppConfig::DisplayPower::DimBrightnessPercent);
      DisplayUi::setEnabled(true);
      break;

    case ScreenState::AutoSleeping:
    case ScreenState::ManualSleeping:
      DisplayUi::setEnabled(false);
      break;
  }

  appliedState = state;
  DiagnosticLog::printf("Display power state -> %s.\n", stateName(state));
}

}

void begin(uint32_t nowMs) {
  activityDetector.reset();
  policy.reset(nowMs);
  initialized = true;
  previousBmsLinkConnected = false;
  appliedState = ScreenState::ManualSleeping;
  applyState(ScreenState::Bright);
}

void observeBmsData(const BmsData &data) {
  if (!initialized || !data.valid || !isfinite(data.current)) return;
  activityDetector.observe(data.current, data.updatedAt);
}

void loop(uint32_t nowMs,
          bool configurationClientConnected,
          bool bmsLinkConnected) {
  if (!initialized) return;

  if (!bmsLinkConnected && previousBmsLinkConnected) {
    activityDetector.reset();
  }
  previousBmsLinkConnected = bmsLinkConnected;

  const RuntimeSettings::DisplayPowerSettings settings =
      RuntimeSettings::displayPowerSettings();
  const VehicleActivity detectedActivity = activityDetector.classify(nowMs);
  const VehicleActivity effectiveActivity =
      DisplayPowerLogic::resolveActivityForLink(detectedActivity,
                                                bmsLinkConnected);

  policy.update(nowMs,
                effectiveActivity,
                settings.enabled,
                configurationClientConnected,
                static_cast<uint32_t>(settings.dimMinutes()) * 60000UL,
                static_cast<uint32_t>(settings.sleepMinutes) * 60000UL);
  applyState(policy.state());
}

void toggleManualState() {
  if (!initialized) return;
  policy.toggleManual(millis());
  applyState(policy.state());
}

void notifyUserInteraction() {
  if (!initialized) return;
  policy.notifyUserInteraction(millis());
  applyState(policy.state());
}

}
