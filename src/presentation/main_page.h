#pragma once

#include "../bms_model.h"

namespace MainPage {

void begin();

void loop(uint32_t nowMs);

void update(const BmsData &data);

void updatePreview(const BmsData &data,
                   float previewRangeKm,
                   float previewPowerW);

void setConnected(bool connected);

}
