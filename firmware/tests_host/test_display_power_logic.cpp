#include <assert.h>
#include <stdint.h>

#include "../src/app_config.h"
#include "../src/power/display_power_logic.h"

using namespace DisplayPowerLogic;

static void testActivityDetector() {
  ActivityThresholds thresholds;
  VehicleActivityDetector detector(thresholds);

  detector.observe(-10.0f, 1000U);
  assert(detector.classify(1000U) == VehicleActivity::Active);

  detector.reset();
  detector.observe(-0.8f, 1000U);
  detector.observe(-0.8f, 4000U);
  assert(detector.classify(4000U) == VehicleActivity::Stationary);

  detector.reset();
  detector.observe(-0.2f, 1000U);
  detector.observe(-1.1f, 4000U);
  assert(detector.classify(4000U) == VehicleActivity::Active);

  detector.reset();
  detector.observe(8.0f, 1000U);
  detector.observe(8.1f, 4000U);
  assert(detector.classify(4000U) == VehicleActivity::Stationary);
  assert(detector.classify(13000U) == VehicleActivity::Unknown);
}

static void testLinkFallback() {

  assert(resolveActivityForLink(VehicleActivity::Unknown, false) ==
         VehicleActivity::Stationary);
  assert(resolveActivityForLink(VehicleActivity::Active, false) ==
         VehicleActivity::Stationary);

  assert(resolveActivityForLink(VehicleActivity::Unknown, true) ==
         VehicleActivity::Unknown);
  assert(resolveActivityForLink(VehicleActivity::Active, true) ==
         VehicleActivity::Active);
}

static void testDisconnectedAutoSleep() {
  DisplayPowerPolicy policy;
  policy.reset(0U);

  const VehicleActivity noLinkActivity =
      resolveActivityForLink(VehicleActivity::Unknown, false);

  policy.update(0U, noLinkActivity, true, false,
                2U * 60000U, 5U * 60000U);
  assert(policy.state() == ScreenState::Bright);

  policy.update(2U * 60000U, noLinkActivity, true, false,
                2U * 60000U, 5U * 60000U);
  assert(policy.state() == ScreenState::Dimmed);

  policy.update(5U * 60000U, noLinkActivity, true, false,
                2U * 60000U, 5U * 60000U);
  assert(policy.state() == ScreenState::AutoSleeping);

  policy.reset(0U);
  policy.update(5U * 60000U, noLinkActivity, true, true,
                2U * 60000U, 5U * 60000U);
  assert(policy.state() == ScreenState::Bright);
}

static void testPowerPolicy() {
  DisplayPowerPolicy policy;
  policy.reset(0U);

  policy.update(0U, VehicleActivity::Stationary, true, false,
                2U * 60000U, 5U * 60000U);
  assert(policy.state() == ScreenState::Bright);

  policy.update(2U * 60000U, VehicleActivity::Stationary, true, false,
                2U * 60000U, 5U * 60000U);
  assert(policy.state() == ScreenState::Dimmed);

  policy.update(5U * 60000U, VehicleActivity::Stationary, true, false,
                2U * 60000U, 5U * 60000U);
  assert(policy.state() == ScreenState::AutoSleeping);

  policy.update(5U * 60000U + 1U, VehicleActivity::Active, true, false,
                2U * 60000U, 5U * 60000U);
  assert(policy.state() == ScreenState::Bright);

  policy.toggleManual(400000U);
  assert(policy.state() == ScreenState::ManualSleeping);
  policy.update(400100U, VehicleActivity::Active, true, false,
                2U * 60000U, 5U * 60000U);
  assert(policy.state() == ScreenState::ManualSleeping);
  policy.toggleManual(400200U);
  assert(policy.state() == ScreenState::Bright);

  policy.update(400300U, VehicleActivity::Stationary, true, true,
                2U * 60000U, 5U * 60000U);
  assert(policy.state() == ScreenState::Bright);
}

int main() {
  static_assert(AppConfig::DisplayPower::derivedDimMinutes(5U) == 2U,
                "5-minute sleep must dim at 2 minutes");
  static_assert(AppConfig::DisplayPower::derivedDimMinutes(6U) == 3U,
                "6-minute sleep must dim at 3 minutes");
  static_assert(AppConfig::DisplayPower::derivedDimMinutes(10U) == 7U,
                "10-minute sleep must dim at 7 minutes");
  testActivityDetector();
  testLinkFallback();
  testDisconnectedAutoSleep();
  testPowerPolicy();
  return 0;
}
