#include "main_page.h"

#include <Arduino.h>
#include <lvgl.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>

#include "dashboard_status_logic.h"
#include "dashboard_ui_config.h"
#include "../core/runtime_settings.h"
#include "../ui/fonts.h"
#include "../ui/images.h"
#include "../ui/screens.h"

namespace MainPage {
namespace {

namespace Cfg = DashboardUiConfig;

constexpr size_t kTextBufferSize = 48;

struct BoldLabel {
  lv_obj_t *primary = nullptr;
  lv_obj_t *offsetX = nullptr;
  lv_obj_t *offsetY = nullptr;
};

lv_obj_t *frameImage = nullptr;
lv_obj_t *socArcs[Cfg::SocRing::CellCount] = {};
lv_obj_t *bluetoothLines[3] = {nullptr, nullptr, nullptr};
uint8_t lastSocRingValue = 255U;
bool lastSocRingWasLow = false;
bool chargingActive = false;
DashboardStatusLogic::ChargingStateDetector chargingDetector;
bool chargeIconVisible = false;
uint32_t chargeBlinkStartedAt = 0U;
bool initialized = false;

BoldLabel socValue;
BoldLabel rangeValue;
BoldLabel capacityValue;
BoldLabel voltageValue;
BoldLabel currentValue;
BoldLabel powerValue;
BoldLabel temperatureValue;
BoldLabel deltaValue;
BoldLabel cycleValue;

BoldLabel bmsTitle;
BoldLabel socTitle;
lv_obj_t *chargeIconLine = nullptr;
BoldLabel percentUnit;
BoldLabel rangeTitle;
BoldLabel rangeUnit;
BoldLabel capacityTitle;
BoldLabel capacityUnit;
BoldLabel voltageTitle;
BoldLabel currentTitle;
BoldLabel powerTitle;
BoldLabel temperatureTitle;
BoldLabel deltaTitle;
BoldLabel cycleTitle;
BoldLabel voltageUnit;
BoldLabel currentUnit;
BoldLabel powerUnit;
BoldLabel temperatureUnit;
BoldLabel deltaUnit;
BoldLabel cycleUnit;

const lv_font_t *resolveFont(Cfg::FontId fontId) {
  switch (fontId) {
    case Cfg::FontMontserrat14:
      return &lv_font_montserrat_14;
    case Cfg::FontMontserrat16:
      return &lv_font_montserrat_16;
    case Cfg::FontMontserrat22:
      return &lv_font_montserrat_22;
    case Cfg::FontMontserrat32:
      return &lv_font_montserrat_32;
    case Cfg::FontMontserrat48:
      return &lv_font_montserrat_48;
    case Cfg::FontChinese16:
      return &ui_font_found_cn_16;
    default:
      return &lv_font_montserrat_14;
  }
}

lv_text_align_t resolveAlignment(Cfg::AlignId alignId) {
  switch (alignId) {
    case Cfg::AlignCenter:
      return LV_TEXT_ALIGN_CENTER;
    case Cfg::AlignRight:
      return LV_TEXT_ALIGN_RIGHT;
    case Cfg::AlignLeft:
    default:
      return LV_TEXT_ALIGN_LEFT;
  }
}

int16_t uiX(int16_t x) {
  return static_cast<int16_t>(x + Cfg::GlobalOffsetX);
}

int16_t uiY(int16_t y) {
  return static_cast<int16_t>(y + Cfg::GlobalOffsetY);
}

void styleLabelObject(lv_obj_t *label,
                      const Cfg::LabelStyle &style,
                      int16_t offsetX,
                      int16_t offsetY) {
  if (label == nullptr) return;

  lv_obj_set_pos(label,
                 uiX(static_cast<int16_t>(style.rect.x + offsetX)),
                 uiY(static_cast<int16_t>(style.rect.y + offsetY)));
  lv_obj_set_size(label, style.rect.width, style.rect.height);
  lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
  lv_obj_clear_flag(label, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE |
                               LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_style_bg_opa(label, LV_OPA_TRANSP,
                          LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_border_width(label, 0,
                                LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_pad_all(label, 0,
                           LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_shadow_width(label, 0,
                                LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_font(label, resolveFont(style.font),
                             LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_color(label, lv_color_hex(style.color),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_opa(label, LV_OPA_COVER,
                            LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_align(label, resolveAlignment(style.align),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_letter_space(label, style.letterSpacing,
                                     LV_PART_MAIN | LV_STATE_DEFAULT);

  lv_obj_set_style_transform_zoom(label, style.zoom,
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
}

lv_obj_t *createLabelObject() {
  if (objects.main == nullptr) return nullptr;
  lv_obj_t *label = lv_label_create(objects.main);
  lv_label_set_text_static(label, "");
  return label;
}

void configureBoldLabel(BoldLabel &target,
                        lv_obj_t *primary,
                        const Cfg::LabelStyle &style) {
  target.primary = primary;
  if (target.primary == nullptr) return;

  if (style.boldLevel >= 1U && target.offsetX == nullptr) {
    target.offsetX = createLabelObject();
  }
  if (style.boldLevel >= 2U && target.offsetY == nullptr) {
    target.offsetY = createLabelObject();
  }

  if (target.offsetX != nullptr) {
    styleLabelObject(target.offsetX, style, 1, 0);
    lv_obj_move_foreground(target.offsetX);
  }
  if (target.offsetY != nullptr) {
    styleLabelObject(target.offsetY, style, 0, 1);
    lv_obj_move_foreground(target.offsetY);
  }

  styleLabelObject(target.primary, style, 0, 0);
  lv_obj_move_foreground(target.primary);
}

void createAndConfigureStaticLabel(BoldLabel &target,
                                   const Cfg::LabelStyle &style,
                                   const char *text) {
  if (target.primary == nullptr) target.primary = createLabelObject();
  configureBoldLabel(target, target.primary, style);

  if (target.offsetX != nullptr) lv_label_set_text(target.offsetX, text);
  if (target.offsetY != nullptr) lv_label_set_text(target.offsetY, text);
  if (target.primary != nullptr) lv_label_set_text(target.primary, text);
}

void setBoldText(BoldLabel &target, const char *format, ...) {
  if (format == nullptr) return;

  char text[kTextBufferSize];
  va_list args;
  va_start(args, format);
  vsnprintf(text, sizeof(text), format, args);
  va_end(args);

  if (target.offsetX != nullptr) lv_label_set_text(target.offsetX, text);
  if (target.offsetY != nullptr) lv_label_set_text(target.offsetY, text);
  if (target.primary != nullptr) lv_label_set_text(target.primary, text);
}

void setObjectVisible(lv_obj_t *object, bool visible) {
  if (object == nullptr) return;
  if (visible) {
    lv_obj_clear_flag(object, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(object, LV_OBJ_FLAG_HIDDEN);
  }
}

void setBoldLabelVisible(BoldLabel &target, bool visible) {
  setObjectVisible(target.offsetX, visible);
  setObjectVisible(target.offsetY, visible);
  setObjectVisible(target.primary, visible);
}

void applyChargingIndicatorVisibility(bool iconVisible) {

  setBoldLabelVisible(socTitle, !chargingActive);
  setObjectVisible(chargeIconLine, chargingActive && iconVisible);
  chargeIconVisible = chargingActive && iconVisible;
}

void setChargingState(bool charging, uint32_t nowMs) {
  if (charging != chargingActive) {
    chargingActive = charging;
    chargeBlinkStartedAt = nowMs;
  }

  const bool visible = chargingActive
                           ? DashboardStatusLogic::blinkVisible(
                                 nowMs - chargeBlinkStartedAt,
                                 Cfg::Elements::Charging::BlinkIntervalMs)
                           : false;
  applyChargingIndicatorVisibility(visible);
}

void createFrame() {
  if (frameImage != nullptr || objects.main == nullptr) return;

  lv_obj_set_style_bg_color(objects.main, lv_color_hex(Cfg::Theme::BackgroundColor),
                            LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_opa(objects.main, LV_OPA_COVER,
                          LV_PART_MAIN | LV_STATE_DEFAULT);

  frameImage = lv_img_create(objects.main);
  lv_img_set_src(frameImage, &img_dashboard_frame);
  lv_obj_set_pos(frameImage,
                 uiX(Cfg::Frame::X),
                 uiY(Cfg::Frame::Y));
  lv_obj_clear_flag(frameImage,
                    LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_set_style_img_recolor(frameImage,
                               lv_color_hex(Cfg::Theme::FrameColor),
                               LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_img_recolor_opa(frameImage, LV_OPA_COVER,
                                   LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_move_background(frameImage);
}

void createSocRing() {
  if (objects.main == nullptr) return;

  for (uint8_t cell = 0U; cell < Cfg::SocRing::CellCount; ++cell) {
    if (socArcs[cell] == nullptr) socArcs[cell] = lv_arc_create(objects.main);

    lv_obj_t *arc = socArcs[cell];
    lv_obj_set_pos(arc, uiX(Cfg::SocRing::X), uiY(Cfg::SocRing::Y));
    lv_obj_set_size(arc, Cfg::SocRing::Size, Cfg::SocRing::Size);
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP,
                            LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(arc, 0,
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(arc, 0,
                             LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_width(arc, 0,
                               LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_width(arc, Cfg::SocRing::Width,
                               LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_color(arc,
                               lv_color_hex(Cfg::Theme::SocRingColor),
                               LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_opa(arc, LV_OPA_COVER,
                             LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_rounded(arc, false,
                                 LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_remove_style(arc, nullptr, LV_PART_KNOB);
    lv_arc_set_bg_angles(arc, 0, 0);

    const int16_t startAngle = static_cast<int16_t>(
        (Cfg::SocRing::StartAngle +
         cell * Cfg::SocRing::CellPitchDegrees) % 360);
    lv_arc_set_angles(arc,
                      startAngle,
                      static_cast<int16_t>(startAngle +
                                           Cfg::SocRing::CellSweepDegrees));
    lv_obj_add_flag(arc, LV_OBJ_FLAG_HIDDEN);
  }
}

void updateSocRing(uint8_t soc) {
  soc = DashboardStatusLogic::clampSocPercent(soc);
  const bool lowSoc = DashboardStatusLogic::isLowSoc(
      soc, Cfg::SocRing::LowSocThresholdPercent);

  if (soc == lastSocRingValue && lowSoc == lastSocRingWasLow) return;
  lastSocRingValue = soc;
  lastSocRingWasLow = lowSoc;

  const uint32_t ringColor = lowSoc
                                 ? Cfg::Theme::LowSocRingColor
                                 : Cfg::Theme::SocRingColor;
  const uint8_t visibleCells = DashboardStatusLogic::visibleSocCellCount(
      soc, Cfg::SocRing::CellCount);

  for (uint8_t cell = 0U; cell < Cfg::SocRing::CellCount; ++cell) {
    lv_obj_t *arc = socArcs[cell];
    if (arc == nullptr) continue;

    lv_obj_set_style_arc_color(arc,
                               lv_color_hex(ringColor),
                               LV_PART_INDICATOR | LV_STATE_DEFAULT);
    if (cell < visibleCells) {
      lv_obj_clear_flag(arc, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(arc, LV_OBJ_FLAG_HIDDEN);
    }
  }
}

void configureStaticLabels() {

  createAndConfigureStaticLabel(bmsTitle,
                                Cfg::Elements::Bms::Label,
                                Cfg::Elements::Bms::Text);

  createAndConfigureStaticLabel(socTitle,
                                Cfg::Elements::Soc::Title,
                                Cfg::Elements::Soc::TitleText);
  createAndConfigureStaticLabel(percentUnit,
                                Cfg::Elements::Soc::Unit,
                                Cfg::Elements::Soc::UnitText);

  createAndConfigureStaticLabel(rangeTitle,
                                Cfg::Elements::Range::Title,
                                Cfg::Elements::Range::TitleText);
  createAndConfigureStaticLabel(rangeUnit,
                                Cfg::Elements::Range::Unit,
                                Cfg::Elements::Range::UnitText);

  createAndConfigureStaticLabel(capacityTitle,
                                Cfg::Elements::Capacity::Title,
                                Cfg::Elements::Capacity::TitleText);
  createAndConfigureStaticLabel(capacityUnit,
                                Cfg::Elements::Capacity::Unit,
                                Cfg::Elements::Capacity::UnitText);

  createAndConfigureStaticLabel(voltageTitle,
                                Cfg::Elements::Voltage::Title,
                                Cfg::Elements::Voltage::TitleText);
  createAndConfigureStaticLabel(voltageUnit,
                                Cfg::Elements::Voltage::Unit,
                                Cfg::Elements::Voltage::UnitText);

  createAndConfigureStaticLabel(currentTitle,
                                Cfg::Elements::Current::Title,
                                Cfg::Elements::Current::TitleText);
  createAndConfigureStaticLabel(currentUnit,
                                Cfg::Elements::Current::Unit,
                                Cfg::Elements::Current::UnitText);

  createAndConfigureStaticLabel(powerTitle,
                                Cfg::Elements::Power::Title,
                                Cfg::Elements::Power::TitleText);
  createAndConfigureStaticLabel(powerUnit,
                                Cfg::Elements::Power::Unit,
                                Cfg::Elements::Power::UnitText);

  createAndConfigureStaticLabel(temperatureTitle,
                                Cfg::Elements::Temperature::Title,
                                Cfg::Elements::Temperature::TitleText);
  createAndConfigureStaticLabel(temperatureUnit,
                                Cfg::Elements::Temperature::Unit,
                                Cfg::Elements::Temperature::UnitText);

  createAndConfigureStaticLabel(deltaTitle,
                                Cfg::Elements::DeltaVoltage::Title,
                                Cfg::Elements::DeltaVoltage::TitleText);
  createAndConfigureStaticLabel(deltaUnit,
                                Cfg::Elements::DeltaVoltage::Unit,
                                Cfg::Elements::DeltaVoltage::UnitText);

  createAndConfigureStaticLabel(cycleTitle,
                                Cfg::Elements::Cycle::Title,
                                Cfg::Elements::Cycle::TitleText);
  createAndConfigureStaticLabel(cycleUnit,
                                Cfg::Elements::Cycle::Unit,
                                Cfg::Elements::Cycle::UnitText);
}

void configureDynamicLabels() {

  configureBoldLabel(socValue,
                     objects.label_soc,
                     Cfg::Elements::Soc::Value);
  configureBoldLabel(rangeValue,
                     objects.label_range_value,
                     Cfg::Elements::Range::Value);
  configureBoldLabel(capacityValue,
                     objects.label_capacity_value,
                     Cfg::Elements::Capacity::Value);
  configureBoldLabel(voltageValue,
                     objects.label_total_voltage_value,
                     Cfg::Elements::Voltage::Value);
  configureBoldLabel(currentValue,
                     objects.label_current_value,
                     Cfg::Elements::Current::Value);
  configureBoldLabel(powerValue,
                     objects.label_power_value,
                     Cfg::Elements::Power::Value);
  configureBoldLabel(temperatureValue,
                     objects.label_temperature_value,
                     Cfg::Elements::Temperature::Value);
  configureBoldLabel(deltaValue,
                     objects.label_delta_value,
                     Cfg::Elements::DeltaVoltage::Value);
  configureBoldLabel(cycleValue,
                     objects.label_cycle_value,
                     Cfg::Elements::Cycle::Value);
}

void createChargeIcon() {
  if (objects.main == nullptr || chargeIconLine != nullptr) return;

  static lv_point_t boltPoints[7];
  const int16_t x = uiX(Cfg::Elements::Charging::Icon.rect.x);
  const int16_t y = uiY(Cfg::Elements::Charging::Icon.rect.y);

  boltPoints[0] = {static_cast<lv_coord_t>(x + 29),
                   static_cast<lv_coord_t>(y + 1)};
  boltPoints[1] = {static_cast<lv_coord_t>(x + 12),
                   static_cast<lv_coord_t>(y + 15)};
  boltPoints[2] = {static_cast<lv_coord_t>(x + 21),
                   static_cast<lv_coord_t>(y + 15)};
  boltPoints[3] = {static_cast<lv_coord_t>(x + 10),
                   static_cast<lv_coord_t>(y + 29)};
  boltPoints[4] = {static_cast<lv_coord_t>(x + 36),
                   static_cast<lv_coord_t>(y + 10)};
  boltPoints[5] = {static_cast<lv_coord_t>(x + 25),
                   static_cast<lv_coord_t>(y + 10)};
  boltPoints[6] = boltPoints[0];

  chargeIconLine = lv_line_create(objects.main);
  lv_line_set_points(chargeIconLine, boltPoints, 7U);
  lv_obj_set_style_line_color(
      chargeIconLine,
      lv_color_hex(Cfg::Elements::Charging::Icon.color),
      LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_line_width(chargeIconLine,
                              Cfg::Elements::Charging::LineWidth,
                              LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_line_rounded(chargeIconLine, false,
                                LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_clear_flag(chargeIconLine,
                    LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(chargeIconLine, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(chargeIconLine);
}

lv_obj_t *createBluetoothLine(lv_point_t *points, uint16_t pointCount) {
  lv_obj_t *line = lv_line_create(objects.main);
  lv_line_set_points(line, points, pointCount);
  lv_obj_set_style_line_color(line,
                              lv_color_hex(Cfg::Theme::BluetoothColor),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_line_width(line, Cfg::Bluetooth::LineWidth,
                              LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_line_rounded(line, true,
                                LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_clear_flag(line, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
  return line;
}

void createBluetoothIcon() {
  if (objects.main == nullptr || bluetoothLines[0] != nullptr) return;

  static lv_point_t spine[2];
  static lv_point_t upperBranch[3];
  static lv_point_t lowerBranch[3];

  const int16_t cx = uiX(Cfg::Bluetooth::CenterX);
  const int16_t cy = uiY(Cfg::Bluetooth::CenterY);
  const int16_t hh = Cfg::Bluetooth::HalfHeight;
  const int16_t hw = Cfg::Bluetooth::HalfWidth;

  spine[0] = {cx, static_cast<lv_coord_t>(cy - hh)};
  spine[1] = {cx, static_cast<lv_coord_t>(cy + hh)};

  upperBranch[0] = {cx, static_cast<lv_coord_t>(cy - hh)};
  upperBranch[1] = {static_cast<lv_coord_t>(cx + hw),
                    static_cast<lv_coord_t>(cy - 3)};
  upperBranch[2] = {static_cast<lv_coord_t>(cx - hw + 1),
                    static_cast<lv_coord_t>(cy + 5)};

  lowerBranch[0] = {cx, static_cast<lv_coord_t>(cy + hh)};
  lowerBranch[1] = {static_cast<lv_coord_t>(cx + hw),
                    static_cast<lv_coord_t>(cy + 3)};
  lowerBranch[2] = {static_cast<lv_coord_t>(cx - hw + 1),
                    static_cast<lv_coord_t>(cy - 5)};

  bluetoothLines[0] = createBluetoothLine(spine, 2);
  bluetoothLines[1] = createBluetoothLine(upperBranch, 3);
  bluetoothLines[2] = createBluetoothLine(lowerBranch, 3);

  for (uint8_t i = 0; i < 3; ++i) {
    lv_obj_move_foreground(bluetoothLines[i]);
  }
}

void showPlaceholders() {
  chargingDetector.reset();
  setChargingState(false, millis());
  updateSocRing(0U);
  setBoldText(socValue, "--");
  setBoldText(rangeValue, "--");
  setBoldText(capacityValue, "--/--");
  setBoldText(voltageValue, "--");
  setBoldText(currentValue, "--");
  setBoldText(powerValue, "--");
  setBoldText(temperatureValue, "--");
  setBoldText(deltaValue, "--");
  setBoldText(cycleValue, "--");
}

void setRangeText(float rangeKm) {
  if (!isfinite(rangeKm) || rangeKm < 0.0f) rangeKm = 0.0f;
  if (rangeKm < 1000.0f) {
    setBoldText(rangeValue,
                "%.*f",
                static_cast<int>(Cfg::Formatting::RangeDecimals),
                rangeKm);
  } else {
    setBoldText(rangeValue, "%.0f", rangeKm);
  }
}

void setPowerText(float powerW) {
  if (!isfinite(powerW)) powerW = 0.0f;
  if (fabsf(powerW) < 1000.0f) {
    setBoldText(powerValue,
                "%.*f",
                static_cast<int>(Cfg::Formatting::PowerDecimals),
                powerW);
  } else {
    setBoldText(powerValue, "%.0f", powerW);
  }
}

void renderData(const BmsData &data,
                bool usePreviewOverrides,
                float previewRangeKm,
                float previewPowerW) {
  if (!data.valid) {
    showPlaceholders();
    return;
  }

  const uint8_t soc = DashboardStatusLogic::clampSocPercent(data.soc);
  updateSocRing(soc);
  setBoldText(socValue, "%u", static_cast<unsigned>(soc));

  const bool charging = chargingDetector.update(
      data,
      Cfg::Elements::Charging::CurrentThresholdA,
      Cfg::Elements::Charging::CurrentExitThresholdA);
  setChargingState(charging, millis());

  if (usePreviewOverrides) {
    setRangeText(previewRangeKm);
  } else {
    const float coefficient = RuntimeSettings::rangeCoefficientKmPerAh();
    setRangeText(data.remainingRangeKm(coefficient));
  }

  if (Cfg::Formatting::CapacitySpacesAroundSlash) {
    setBoldText(capacityValue,
                "%.*f / %.*f",
                static_cast<int>(Cfg::Formatting::CapacityDecimals),
                data.remainingCapacityAh,
                static_cast<int>(Cfg::Formatting::CapacityDecimals),
                data.totalCapacityAh);
  } else {
    setBoldText(capacityValue,
                "%.*f/%.*f",
                static_cast<int>(Cfg::Formatting::CapacityDecimals),
                data.remainingCapacityAh,
                static_cast<int>(Cfg::Formatting::CapacityDecimals),
                data.totalCapacityAh);
  }

  setBoldText(voltageValue,
              "%.*f",
              static_cast<int>(Cfg::Formatting::VoltageDecimals),
              data.totalVoltage);
  setBoldText(currentValue,
              "%.*f",
              static_cast<int>(Cfg::Formatting::CurrentDecimals),
              data.current);

  if (usePreviewOverrides) {
    setPowerText(previewPowerW);
  } else {
    const float calculatedPower = data.totalVoltage * data.current;
    const float powerW = isfinite(calculatedPower)
                             ? calculatedPower
                             : static_cast<float>(data.power);
    setPowerText(powerW);
  }

  setBoldText(temperatureValue, "%d", data.mosTemperature);

  float deltaVoltage = data.deltaCellVoltage;
  if (!isfinite(deltaVoltage) || deltaVoltage < 0.0f) {
    deltaVoltage = 0.0f;
  }
  setBoldText(deltaValue,
              "%.*f",
              static_cast<int>(Cfg::Formatting::DeltaVoltageDecimals),
              deltaVoltage);

  setBoldText(cycleValue, "%u",
              static_cast<unsigned>(data.cycleCount()));
}

}

void begin() {
  if (initialized) return;

  createFrame();
  createSocRing();
  configureStaticLabels();
  configureDynamicLabels();
  createChargeIcon();
  createBluetoothIcon();
  initialized = true;

  setConnected(false);
  showPlaceholders();
}

void loop(uint32_t nowMs) {
  if (!initialized || !chargingActive) return;

  const bool visible = DashboardStatusLogic::blinkVisible(
      nowMs - chargeBlinkStartedAt,
      Cfg::Elements::Charging::BlinkIntervalMs);
  if (visible != chargeIconVisible) {
    applyChargingIndicatorVisibility(visible);
  }
}

void update(const BmsData &data) {
  if (!initialized) return;
  renderData(data, false, 0.0f, 0.0f);
}

void updatePreview(const BmsData &data,
                   float previewRangeKm,
                   float previewPowerW) {
  if (!initialized) return;
  renderData(data, true, previewRangeKm, previewPowerW);
}

void setConnected(bool connected) {
  for (uint8_t i = 0; i < 3; ++i) {
    if (bluetoothLines[i] == nullptr) continue;
    if (connected) {
      lv_obj_clear_flag(bluetoothLines[i], LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(bluetoothLines[i], LV_OBJ_FLAG_HIDDEN);
    }
  }
}

}
