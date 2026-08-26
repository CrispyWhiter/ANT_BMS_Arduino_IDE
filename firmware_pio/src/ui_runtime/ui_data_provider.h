#pragma once

#include <Arduino.h>

#include "../bms_model.h"
#include "bmsui_protocol.h"

namespace UiDataProvider {

struct Context {
  const BmsData *data = nullptr;
  bool connected = false;
  bool usePreviewOverrides = false;
  float previewRangeKm = 0.0f;
  float previewPowerW = 0.0f;
};

bool numericValue(BmsUi::DataId id, const Context &context, float &value);
bool progressRatio(BmsUi::DataId id, const Context &context, float &ratio);
void formatLabelValue(const BmsUi::WidgetSpec &spec,
                      const Context &context,
                      char *output,
                      size_t outputSize);
void formatLabel(const BmsUi::WidgetSpec &spec,
                 const Context &context,
                 char *output,
                 size_t outputSize);

}  // namespace UiDataProvider
