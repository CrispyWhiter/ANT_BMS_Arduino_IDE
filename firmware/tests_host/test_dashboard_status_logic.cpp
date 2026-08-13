#include <assert.h>
#include <math.h>
#include <stdint.h>

#include "../src/presentation/dashboard_status_logic.h"

uint32_t millis() {
  return 0U;
}

int main() {
  using namespace DashboardStatusLogic;

  assert(clampSocPercent(0U) == 0U);
  assert(clampSocPercent(25U) == 25U);
  assert(clampSocPercent(101U) == 100U);

  assert(isLowSoc(0U, 25U));
  assert(isLowSoc(25U, 25U));
  assert(!isLowSoc(26U, 25U));
  assert(isLowSoc(100U, 120U));

  assert(visibleSocCellCount(0U, 60U) == 0U);
  assert(visibleSocCellCount(1U, 60U) == 1U);
  assert(visibleSocCellCount(25U, 60U) == 15U);
  assert(visibleSocCellCount(26U, 60U) == 16U);
  assert(visibleSocCellCount(50U, 60U) == 30U);
  assert(visibleSocCellCount(75U, 60U) == 45U);
  assert(visibleSocCellCount(99U, 60U) == 60U);
  assert(visibleSocCellCount(100U, 60U) == 60U);
  assert(visibleSocCellCount(101U, 60U) == 60U);
  assert(visibleSocCellCount(50U, 0U) == 0U);

  BmsData data{};
  data.current = 20.0f;
  assert(!isCharging(data, 0.3f));

  data.valid = true;
  data.current = -12.5f;
  assert(!isCharging(data, 0.3f));
  data.current = 0.29f;
  assert(!isCharging(data, 0.3f));
  data.current = 0.30f;
  assert(isCharging(data, 0.3f));
  data.current = 12.5f;
  data.chargeMos = 0U;
  assert(isCharging(data, 0.3f));
  data.current = NAN;
  assert(!isCharging(data, 0.3f));
  data.current = 1.0f;
  assert(!isCharging(data, 0.0f));
  assert(!isCharging(data, NAN));

  ChargingStateDetector chargingDetector;
  data.valid = true;
  data.current = 0.49f;
  assert(!chargingDetector.update(data, 0.50f, 0.15f));
  data.current = 0.50f;
  assert(chargingDetector.update(data, 0.50f, 0.15f));
  data.current = 0.20f;
  assert(chargingDetector.update(data, 0.50f, 0.15f));
  data.current = 0.15f;
  assert(!chargingDetector.update(data, 0.50f, 0.15f));
  data.current = -20.0f;
  assert(!chargingDetector.update(data, 0.50f, 0.15f));
  data.valid = false;
  data.current = 10.0f;
  assert(!chargingDetector.update(data, 0.50f, 0.15f));

  assert(blinkVisible(0U, 500U));
  assert(blinkVisible(499U, 500U));
  assert(!blinkVisible(500U, 500U));
  assert(!blinkVisible(999U, 500U));
  assert(blinkVisible(1000U, 500U));
  assert(blinkVisible(12345U, 0U));

  return 0;
}
