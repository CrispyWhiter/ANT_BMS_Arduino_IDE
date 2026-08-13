#include "ui_data_provider.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../core/runtime_settings.h"

namespace UiDataProvider {
namespace {

bool requiresValidBms(BmsUi::DataId id) {
  return id != BmsUi::DataId::None && id != BmsUi::DataId::Connected;
}

float computePower(const BmsData &data) {
  const float calculated = data.totalVoltage * data.current;
  return isfinite(calculated) ? calculated : static_cast<float>(data.power);
}

void formatNumber(float value, uint8_t decimals, char *output, size_t outputSize) {
  if (!isfinite(value)) value = 0.0f;
  if (decimals > 3U) decimals = 3U;
  snprintf(output, outputSize, "%.*f", static_cast<int>(decimals), value);
}

bool isStatusBinding(BmsUi::DataId id) {
  return id == BmsUi::DataId::ChargeMos ||
         id == BmsUi::DataId::DischargeMos ||
         id == BmsUi::DataId::BalancerStatus ||
         id == BmsUi::DataId::Connected;
}

}  // namespace

bool numericValue(BmsUi::DataId id, const Context &context, float &value) {
  value = 0.0f;
  if (id == BmsUi::DataId::None) return false;

  if (id == BmsUi::DataId::Connected) {
    value = context.connected ? 1.0f : 0.0f;
    return true;
  }

  const BmsData *data = context.data;
  if (data == nullptr || (requiresValidBms(id) && !data->valid)) return false;

  switch (id) {
    case BmsUi::DataId::Soc: value = data->soc; return true;
    case BmsUi::DataId::Soh: value = data->soh; return true;
    case BmsUi::DataId::TotalVoltage: value = data->totalVoltage; return true;
    case BmsUi::DataId::Current: value = data->current; return true;
    case BmsUi::DataId::Power:
      value = context.usePreviewOverrides ? context.previewPowerW : computePower(*data);
      return true;
    case BmsUi::DataId::RemainingCapacityAh: value = data->remainingCapacityAh; return true;
    case BmsUi::DataId::TotalCapacityAh: value = data->totalCapacityAh; return true;
    case BmsUi::DataId::RemainingRangeKm:
      value = context.usePreviewOverrides
                  ? context.previewRangeKm
                  : data->remainingRangeKm(RuntimeSettings::rangeCoefficientKmPerAh());
      return true;
    case BmsUi::DataId::MosTemperature: value = data->mosTemperature; return true;
    case BmsUi::DataId::DeltaCellVoltage: value = data->deltaCellVoltage; return true;
    case BmsUi::DataId::MaxCellVoltage: value = data->maxCellVoltage; return true;
    case BmsUi::DataId::MinCellVoltage: value = data->minCellVoltage; return true;
    case BmsUi::DataId::AverageCellVoltage: value = data->averageCellVoltage; return true;
    case BmsUi::DataId::CycleCount: value = data->cycleCount(); return true;
    case BmsUi::DataId::CellCount: value = data->cellCount; return true;
    case BmsUi::DataId::MaxCellIndex: value = data->maxCell; return true;
    case BmsUi::DataId::MinCellIndex: value = data->minCell; return true;
    case BmsUi::DataId::ChargeMos: value = data->chargeMos ? 1.0f : 0.0f; return true;
    case BmsUi::DataId::DischargeMos: value = data->dischargeMos ? 1.0f : 0.0f; return true;
    case BmsUi::DataId::BalancerStatus: value = data->balancerStatus ? 1.0f : 0.0f; return true;
    case BmsUi::DataId::Connected:
    case BmsUi::DataId::None:
    default:
      return false;
  }
}

bool progressRatio(BmsUi::DataId id, const Context &context, float &ratio) {
  ratio = 0.0f;

  if (id == BmsUi::DataId::Soc) {
    float soc = 0.0f;
    if (!numericValue(id, context, soc)) return false;
    ratio = soc / 100.0f;
  } else if (id == BmsUi::DataId::RemainingCapacityAh) {
    const BmsData *data = context.data;
    if (data == nullptr || !data->valid || !isfinite(data->remainingCapacityAh) ||
        !isfinite(data->totalCapacityAh) || data->totalCapacityAh <= 0.0f) {
      return false;
    }
    ratio = data->remainingCapacityAh / data->totalCapacityAh;
  } else if (id == BmsUi::DataId::RemainingRangeKm) {
    const BmsData *data = context.data;
    if (data == nullptr || !data->valid || !isfinite(data->totalCapacityAh)) return false;

    const float coefficient = RuntimeSettings::rangeCoefficientKmPerAh();
    if (!isfinite(coefficient) || coefficient <= 0.0f) return false;

    const float usableFullCapacity = data->totalCapacityAh - AppConfig::Range::ReserveCapacityAh;
    if (!isfinite(usableFullCapacity) || usableFullCapacity <= 0.0f) return false;

    float remainingRange = 0.0f;
    if (!numericValue(BmsUi::DataId::RemainingRangeKm, context, remainingRange)) return false;
    const float fullRange = usableFullCapacity * coefficient;
    if (!isfinite(fullRange) || fullRange <= 0.0f) return false;
    ratio = remainingRange / fullRange;
  } else {
    return false;
  }

  if (!isfinite(ratio)) return false;
  if (ratio < 0.0f) ratio = 0.0f;
  if (ratio > 1.0f) ratio = 1.0f;
  return true;
}

void formatLabelValue(const BmsUi::WidgetSpec &spec,
                      const Context &context,
                      char *output,
                      size_t outputSize) {
  if (output == nullptr || outputSize == 0U) return;
  output[0] = '\0';

  if (spec.binding == BmsUi::DataId::None) {
    snprintf(output, outputSize, "%s", spec.text);
    return;
  }

  float value = 0.0f;
  if (!numericValue(spec.binding, context, value)) {
    snprintf(output, outputSize, "%s--", spec.prefix);
    return;
  }

  char valueText[32] = {};
  if (isStatusBinding(spec.binding)) {
    snprintf(valueText, sizeof(valueText), "%s", value >= 0.5f ? "ON" : "OFF");
  } else {
    formatNumber(value, spec.decimals, valueText, sizeof(valueText));
  }

  snprintf(output, outputSize, "%s%s", spec.prefix, valueText);
}

void formatLabel(const BmsUi::WidgetSpec &spec,
                 const Context &context,
                 char *output,
                 size_t outputSize) {
  if (output == nullptr || outputSize == 0U) return;
  output[0] = '\0';

  if (spec.binding == BmsUi::DataId::None) {
    snprintf(output, outputSize, "%s", spec.text);
    return;
  }

  float value = 0.0f;
  if (!numericValue(spec.binding, context, value)) {
    snprintf(output, outputSize, "%s--%s", spec.prefix, spec.suffix);
    return;
  }

  char valueText[32] = {};
  if (isStatusBinding(spec.binding)) {
    snprintf(valueText, sizeof(valueText), "%s", value >= 0.5f ? "ON" : "OFF");
  } else {
    formatNumber(value, spec.decimals, valueText, sizeof(valueText));
  }

  snprintf(output, outputSize, "%s%s%s", spec.prefix, valueText, spec.suffix);
}

}  // namespace UiDataProvider
