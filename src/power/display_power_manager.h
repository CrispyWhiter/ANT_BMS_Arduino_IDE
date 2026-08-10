#pragma once

#include <stdint.h>

#include "../bms_model.h"

namespace DisplayPowerManager {

void begin(uint32_t nowMs);
void observeBmsData(const BmsData &data);
void loop(uint32_t nowMs,
          bool configurationClientConnected,
          bool bmsLinkConnected);
void toggleManualState();
void notifyUserInteraction();

}
