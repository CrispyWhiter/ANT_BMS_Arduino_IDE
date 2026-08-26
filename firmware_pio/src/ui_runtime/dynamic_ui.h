#pragma once

#include <Arduino.h>
#include <lvgl.h>

#include "../bms_model.h"

namespace DynamicUi {

bool begin();

void markBootHealthy();
void loop(uint32_t nowMs);
void update(const BmsData &data);
void updatePreview(const BmsData &data, float previewRangeKm, float previewPowerW);
void setConnected(bool connected);

bool active();
bool packageValid();
lv_obj_t *screen();
const char *lastError();
uint16_t widgetCount();
const char *title();

}  // namespace DynamicUi
