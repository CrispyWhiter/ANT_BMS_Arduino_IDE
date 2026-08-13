#include <assert.h>
#include <math.h>
#include <string.h>
#include <stdio.h>

#include "../src/ui_runtime/ui_data_provider.h"

uint32_t millis() { return 0; }
namespace RuntimeSettings {
float rangeCoefficientKmPerAh() { return 2.5f; }
}

int main() {
  BmsData data;
  data.valid = true;
  data.soc = 78;
  data.totalVoltage = 72.4f;
  data.current = -35.6f;
  data.remainingCapacityAh = 117.0f;
  data.totalCapacityAh = 150.0f;
  data.mosTemperature = 32;
  data.deltaCellVoltage = 0.018f;
  data.chargeMos = 1;

  UiDataProvider::Context context;
  context.data = &data;
  context.connected = true;

  float value = 0.0f;
  assert(UiDataProvider::numericValue(BmsUi::DataId::Soc, context, value));
  assert(fabsf(value - 78.0f) < 0.001f);
  assert(UiDataProvider::numericValue(BmsUi::DataId::Power, context, value));
  assert(value < -2500.0f);
  assert(UiDataProvider::numericValue(BmsUi::DataId::RemainingRangeKm, context, value));
  assert(fabsf(value - 257.5f) < 0.01f);

  float ratio = 0.0f;
  assert(UiDataProvider::progressRatio(BmsUi::DataId::Soc, context, ratio));
  assert(fabsf(ratio - 0.78f) < 0.001f);
  assert(UiDataProvider::progressRatio(BmsUi::DataId::RemainingCapacityAh, context, ratio));
  assert(fabsf(ratio - 0.78f) < 0.001f);
  assert(UiDataProvider::progressRatio(BmsUi::DataId::RemainingRangeKm, context, ratio));
  assert(fabsf(ratio - (257.5f / 340.0f)) < 0.001f);

  BmsUi::WidgetSpec label;
  label.type = BmsUi::WidgetType::Label;
  label.binding = BmsUi::DataId::TotalVoltage;
  label.decimals = 1;
  snprintf(label.prefix, sizeof(label.prefix), "V=");
  snprintf(label.suffix, sizeof(label.suffix), "V");
  char output[64] = {};
  UiDataProvider::formatLabel(label, context, output, sizeof(output));
  assert(strcmp(output, "V=72.4V") == 0);


  label.binding = BmsUi::DataId::None;
  snprintf(label.text, sizeof(label.text), "Battery Status Ready");
  UiDataProvider::formatLabel(label, context, output, sizeof(output));
  assert(strcmp(output, "Battery Status Ready") == 0);

  label.binding = BmsUi::DataId::Connected;
  label.prefix[0] = '\0';
  label.suffix[0] = '\0';
  UiDataProvider::formatLabel(label, context, output, sizeof(output));
  assert(strcmp(output, "ON") == 0);

  BmsData invalid;
  context.data = &invalid;
  label.binding = BmsUi::DataId::Soc;
  snprintf(label.prefix, sizeof(label.prefix), "SOC ");
  snprintf(label.suffix, sizeof(label.suffix), "%%");
  UiDataProvider::formatLabel(label, context, output, sizeof(output));
  assert(strcmp(output, "SOC --%") == 0);
  return 0;
}
