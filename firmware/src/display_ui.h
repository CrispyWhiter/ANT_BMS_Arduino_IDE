#pragma once

#include <Arduino.h>

#include "bms_model.h"

namespace DisplayUi {

enum class Page : uint8_t {
  Main = 0,
  Cells = 1
};

bool begin();

void loop();

void update(const BmsData &data);

void updatePreview(const BmsData &data,
                   float previewRangeKm,
                   float previewPowerW);

void setConnected(bool connected);

void togglePage();

void showPage(Page page);

void setBrightnessPercent(uint8_t percent);

void setEnabled(bool enabled);

bool isEnabled();

}
