#include "application.h"

#include <Arduino.h>
#include <esp_system.h>

#include "app_config.h"
#include "core/runtime_profile.h"
#include "core/bms_data_store.h"
#include "core/diagnostic_log.h"
#include "core/runtime_settings.h"
#include "core/time_utils.h"
#include "display_ui.h"
#include "input/button_controller.h"
#include "presentation/dashboard_test_mode.h"
#include "power/display_power_manager.h"
#include "services/bms_ble_service.h"
#include "services/config_web_portal.h"

namespace Application {
namespace {

enum class PortalLaunchState : uint8_t {
  Idle,
  WaitingForBleScan,
};

uint32_t restartAt = 0;
uint32_t fallbackApAt = 0;
bool portalWasActive = false;
PortalLaunchState portalLaunchState = PortalLaunchState::Idle;
uint32_t portalScanDeadlineAt = 0;
bool initialized = false;
uint32_t nextDashboardTestRefreshAt = 0;

bool dashboardTestModeActive() {
  return DashboardTestMode::Enabled || RuntimeSettings::dashboardTestModeEnabled();
}

void handleSingleClick() {
  if (!DisplayUi::isEnabled()) return;
  DisplayPowerManager::notifyUserInteraction();
  DisplayUi::togglePage();
  DiagnosticLog::write("Button click: page switched and display timer reset.\n");
}

void handleTripleClick() {
  DisplayPowerManager::toggleManualState();
  DiagnosticLog::write("Triple click: display manual state toggled.\n");
}

void handleLongPress() {
  if (dashboardTestModeActive()) {
    DiagnosticLog::write(
        "10-second hold ignored: dashboard test mode protects stored BLE pairing.\n");
    return;
  }

  const BmsType currentType = BmsBleService::pairedBmsType();
  const bool cleared = BmsBleService::clearPairedDevice();
  const bool scanRequested =
      RuntimeSettings::requestConfigurationScanOnNextBoot(currentType);

  DiagnosticLog::write(cleared
                           ? "10-second hold: BLE pairing cleared.\n"
                           : "10-second hold: BLE pairing clear failed.\n");
  if (!scanRequested) {
    DiagnosticLog::write("WARNING: unable to persist one-shot configuration scan flag.\n");
  }

  restartAt = TimeUtils::deadlineAfter(
      millis(), AppConfig::Runtime::LongPressRestartDelayMs);
}

ButtonController userButton(AppConfig::Pins::UserButton,
                            AppConfig::Button::ActiveLevel,
                            AppConfig::Button::DebounceMs,
                            AppConfig::Button::MultiClickWindowMs,
                            AppConfig::Button::ScreenToggleClickCount,
                            AppConfig::Button::ReservedLongPressMs,
                            handleSingleClick,
                            handleTripleClick,
                            handleLongPress);

void refreshDashboardTestData(uint32_t now) {
  const BmsData testData = DashboardTestMode::makeData(now);
  DisplayUi::updatePreview(testData,
                           DashboardTestMode::RemainingRangeKm,
                           DashboardTestMode::powerW());
  DisplayUi::setConnected(DashboardTestMode::ShowBluetoothIcon);
  if (DashboardTestMode::EnableAutoDisplayPowerTest) {
    DisplayPowerManager::observeBmsData(testData);
  }
}

void logStartupBanner() {
  const bool testMode = dashboardTestModeActive();
  DiagnosticLog::printf("%s %s\n",
                        AppConfig::Firmware::Name,
                        AppConfig::Firmware::Version);
  DiagnosticLog::printf("Developer: %s\n", AppConfig::Firmware::Developer);
  DiagnosticLog::write(
      testMode
          ? "Button: click=page, triple-click=screen, hold 10s=ignored in test mode\n"
          : "Button: click=page, triple-click=screen, hold 10s=clear BLE setup\n");
  DiagnosticLog::write(
      "Radio policy: scan selected BMS brand, stop BLE, then start cached-result SoftAP\n");
  DiagnosticLog::printf("Reset reason=%d, boot free heap=%u bytes.\n",
                        static_cast<int>(esp_reset_reason()),
                        static_cast<unsigned>(ESP.getFreeHeap()));
  DiagnosticLog::write(testMode
                           ? "Dashboard test mode: ENABLED; BLE data link is suspended and Web control remains available.\n"
                           : "Dashboard test mode: disabled; real BMS mode.\n");
}

void requestScannedPortal(const char *reason, BmsType scanType) {
  if (portalLaunchState != PortalLaunchState::Idle ||
      ConfigWebPortal::isActive()) {
    return;
  }

  fallbackApAt = 0;
  BmsBleService::setWebPortalActive(false);
  BmsBleService::requestConfigurationScan(scanType);
  portalLaunchState = PortalLaunchState::WaitingForBleScan;
  portalScanDeadlineAt = TimeUtils::deadlineAfter(
      millis(), AppConfig::WebConfig::PrePortalScanTimeoutMs);

  DiagnosticLog::printf(
      "Preparing Web portal: %s; scanning %s devices before AP startup.\n",
      reason == nullptr ? "configuration requested" : reason,
      BmsTypeInfo::displayName(scanType));
}

bool startPortalWithCachedScan() {
  delay(AppConfig::Ble::CallbackExitSettleMs);

  if (!BmsBleService::prepareForWebPortal()) {
    DiagnosticLog::write(
        "BLE controller cleanup reported an error; attempting AP startup.\n");
  }
  delay(AppConfig::WebConfig::RadioSettleDelayMs);

  if (!ConfigWebPortal::begin()) {
    DiagnosticLog::write("Web configuration AP failed to start after BLE scan.\n");
    return false;
  }

  BmsBleService::setWebPortalActive(true);
  portalWasActive = true;
  DiagnosticLog::printf("Web AP started with cached %s scan results.\n",
                        BmsTypeInfo::displayName(
                            BmsBleService::configurationScanType()));
  return true;
}

void servicePortalLaunch(uint32_t now) {
  if (portalLaunchState != PortalLaunchState::WaitingForBleScan) return;

  const bool completed = BmsBleService::isConfigurationScanComplete();
  const bool timedOut = TimeUtils::deadlineReached(now, portalScanDeadlineAt);
  if (!completed && !timedOut) return;

  if (timedOut && !completed) {
    DiagnosticLog::write(
        "Pre-portal BLE scan timed out; opening AP with available cached results.\n");
    BmsBleService::cancelConfigurationScan();
  }

  portalLaunchState = PortalLaunchState::Idle;
  portalScanDeadlineAt = 0;

  if (!startPortalWithCachedScan()) {

    fallbackApAt = TimeUtils::deadlineAfter(
        millis(), AppConfig::WebConfig::PortalRetryDelayMs);
  }
}

}

void begin() {
  if (initialized) return;

  Serial.begin(115200);
  delay(AppConfig::Runtime::SerialReadyDelayMs);

  DiagnosticLog::begin();
  RuntimeSettings::begin();
  logStartupBanner();
  BmsDataStore::reset();
  userButton.begin();

  if (!DisplayUi::begin()) {
    DiagnosticLog::write("Display/LVGL initialization failed.\n");
  }
  DisplayPowerManager::begin(millis());

  if (dashboardTestModeActive()) {
    BmsBleService::begin();
    const uint32_t now = millis();
    refreshDashboardTestData(now);
    nextDashboardTestRefreshAt = TimeUtils::deadlineAfter(
        now, DashboardTestMode::AnimationStepIntervalMs);
    if (!ConfigWebPortal::begin(true)) {
      fallbackApAt = TimeUtils::deadlineAfter(
          now, AppConfig::WebConfig::PortalRetryDelayMs);
      DiagnosticLog::write("Dashboard test Web portal startup failed; retry scheduled.\n");
    } else {
      BmsBleService::setWebPortalActive(true);
    }
    initialized = true;
    DiagnosticLog::write(
        "Dashboard test data displayed; Web control remains available.\n");
    return;
  }

  delay(AppConfig::Runtime::DisplayToBleDelayMs + RuntimeProfile::linkBiasMs());

  if (!BmsBleService::begin()) {
    DiagnosticLog::write("BLE service configuration load failed.\n");
  }

  BmsType requestedScanType = RuntimeSettings::preferredConfigurationScanType();
  const bool forceConfigurationScan =
      RuntimeSettings::consumeConfigurationScanOnNextBoot(requestedScanType);

  if (forceConfigurationScan || !BmsBleService::hasConfiguredDevice()) {

    requestScannedPortal(forceConfigurationScan
                             ? "one-shot brand scan requested"
                             : "no stored BMS target",
                         requestedScanType);
  } else {
    DiagnosticLog::write(
        "Stored BMS target found: BLE starts first; recovery portal is delayed.\n");
    BmsBleService::setWebPortalActive(false);
    fallbackApAt = TimeUtils::deadlineAfter(
        millis(), AppConfig::WebConfig::FallbackAfterBleStartMs);
  }

  initialized = true;
}

void loop() {
  if (!initialized) return;

  DisplayUi::loop();
  userButton.loop();

  if (dashboardTestModeActive()) {
    ConfigWebPortal::loop();
    BmsBleService::setWebPortalActive(ConfigWebPortal::isActive());

    const uint32_t now = millis();
    if (!ConfigWebPortal::isActive() &&
        TimeUtils::deadlineReached(now, fallbackApAt)) {
      fallbackApAt = 0;
      if (!ConfigWebPortal::begin(true)) {
        fallbackApAt = TimeUtils::deadlineAfter(
            now, AppConfig::WebConfig::PortalRetryDelayMs);
      } else {
        BmsBleService::setWebPortalActive(true);
      }
    }

    if (DashboardTestMode::AnimateSoc &&
        TimeUtils::deadlineReached(now, nextDashboardTestRefreshAt)) {
      refreshDashboardTestData(now);
      nextDashboardTestRefreshAt = TimeUtils::deadlineAfter(
          now, DashboardTestMode::AnimationStepIntervalMs);
    }

    if (DashboardTestMode::EnableAutoDisplayPowerTest) {
      DisplayPowerManager::loop(now, ConfigWebPortal::hasClient(), true);
    }
    delay(AppConfig::Runtime::MainLoopYieldMs);
    return;
  }

  ConfigWebPortal::loop();

  bool portalActive = ConfigWebPortal::isActive();
  BmsBleService::setWebPortalActive(portalActive);
  BmsBleService::loop();

  const uint32_t now = millis();
  servicePortalLaunch(now);

  portalActive = ConfigWebPortal::isActive();
  const bool portalClient = ConfigWebPortal::hasClient();
  const bool targetConfigured = BmsBleService::hasConfiguredDevice();
  const bool bmsReady = BmsBleService::hasValidStatus();

  if (portalLaunchState == PortalLaunchState::Idle) {
    if (targetConfigured && bmsReady) {

      fallbackApAt = 0;
      if (portalActive && !portalClient) {
        ConfigWebPortal::stop();
        BmsBleService::setWebPortalActive(false);
        portalWasActive = false;
      }
    } else if (targetConfigured && !portalActive &&
               TimeUtils::deadlineReached(now, fallbackApAt)) {
      fallbackApAt = 0;
      requestScannedPortal("stored target did not produce valid protocol data",
                           BmsBleService::pairedBmsType());
    } else if (!targetConfigured && !portalActive &&
               TimeUtils::deadlineReached(now, fallbackApAt)) {
      fallbackApAt = 0;
      requestScannedPortal("retry after AP startup failure",
                           RuntimeSettings::preferredConfigurationScanType());
    }
  }

  if (portalWasActive && !ConfigWebPortal::isActive() &&
      targetConfigured && !bmsReady) {
    fallbackApAt = TimeUtils::deadlineAfter(
        millis(), AppConfig::WebConfig::FallbackAfterBleStartMs);
  }
  portalWasActive = ConfigWebPortal::isActive();

  if (TimeUtils::deadlineReached(millis(), restartAt)) {
    restartAt = 0;
    delay(AppConfig::Runtime::ForcedRestartFlushDelayMs);
    ESP.restart();
    return;
  }

  BmsData updatedData;
  if (BmsDataStore::consumePendingUpdate(updatedData)) {
    DisplayPowerManager::observeBmsData(updatedData);
    DisplayUi::update(updatedData);
  }

  DisplayPowerManager::loop(millis(),
                            ConfigWebPortal::hasClient(),
                            BmsBleService::isConnected());

  delay(AppConfig::Runtime::MainLoopYieldMs);
}

}
