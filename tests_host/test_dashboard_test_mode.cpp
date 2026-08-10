#include <assert.h>
#include <stdint.h>

#include "../src/presentation/dashboard_status_logic.h"
#include "../src/presentation/dashboard_test_mode.h"
#include "../src/presentation/dashboard_ui_config.h"

uint32_t millis() {
  return 0U;
}

int main() {
  const BmsData data = DashboardTestMode::makeData(0U);

  assert(data.valid);
  assert(data.current > 0.0f);
  assert(DashboardTestMode::powerW() > 0.0f);
  assert(DashboardStatusLogic::isCharging(
      data, DashboardUiConfig::Elements::Charging::CurrentThresholdA));

  assert(data.soc == DashboardTestMode::socPercent(0U));
  assert(DashboardStatusLogic::visibleSocCellCount(data.soc, 60U) == 0U);
  assert(DashboardStatusLogic::visibleSocCellCount(1U, 60U) == 1U);

  return 0;
}
