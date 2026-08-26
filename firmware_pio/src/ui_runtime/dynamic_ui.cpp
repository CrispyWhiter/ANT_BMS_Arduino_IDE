#include "dynamic_ui.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <esp_attr.h>
#include <esp_heap_caps.h>

#include "bmsui_protocol.h"
#include "ui_data_provider.h"
#include "ui_package_store.h"
#include "../core/diagnostic_log.h"
#include "../ui/fonts.h"

namespace DynamicUi {
namespace {

constexpr uint8_t kGradientArcSegments = 32U;
constexpr size_t kPsramUsableThresholdBytes = 1024U * 1024U;
constexpr size_t kLargeUiBufferBytes = 2048U;

bool psramUsable() {
  return heap_caps_get_total_size(MALLOC_CAP_SPIRAM) >= kPsramUsableThresholdBytes;
}

void logMemorySnapshot(const char *stage) {
  const size_t internalFree = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  const size_t internalLargest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  const size_t psramTotal = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
  const size_t psramFree = heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  const size_t psramLargest = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  DiagnosticLog::printf(
      "[MEM] %s: int free=%u largest=%u; psram total=%u free=%u largest=%u.\n",
      stage == nullptr ? "?" : stage,
      static_cast<unsigned>(internalFree),
      static_cast<unsigned>(internalLargest),
      static_cast<unsigned>(psramTotal),
      static_cast<unsigned>(psramFree),
      static_cast<unsigned>(psramLargest));
}

void logPsramConfiguration() {
  const size_t arduinoPsram = ESP.getPsramSize();
  const size_t capsPsram = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
  DiagnosticLog::printf("[PSRAM] Arduino=%u bytes, heap-cap=%u bytes.\n",
                        static_cast<unsigned>(arduinoPsram),
                        static_cast<unsigned>(capsPsram));
  if (capsPsram == 0U) {
    DiagnosticLog::write(
        "[PSRAM] WARNING: PSRAM is not enabled; large custom UI activation will be deferred and the package will be kept.\n");
  } else if (capsPsram < kPsramUsableThresholdBytes) {
    DiagnosticLog::write(
        "[PSRAM] WARNING: PSRAM is present but too small for the PSRAM-first dynamic UI path.\n");
  } else {
    DiagnosticLog::printf(
        "[PSRAM] External RAM is usable for dynamic UI/LVGL resources (%u MiB detected).\n",
        static_cast<unsigned>(capsPsram / (1024U * 1024U)));
  }
}

size_t availableRuntimeAssetLimit() {
  return psramUsable() ? BmsUi::MaxRuntimeAssetBytes : BmsUi::BaseRuntimeAssetBytes;
}

uint8_t *allocateUiBuffer(size_t bytes, bool clear, const char *purpose) {
  if (bytes == 0U) return nullptr;

  uint8_t *data = nullptr;
  const bool havePsram = psramUsable();
  if (havePsram) {
    data = static_cast<uint8_t *>(
        heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (data == nullptr) {
      DiagnosticLog::printf(
          "Dynamic UI: PSRAM allocation failed: %s, %u bytes.\n",
          purpose == nullptr ? "buffer" : purpose,
          static_cast<unsigned>(bytes));
      logMemorySnapshot("PSRAM alloc failure");
    }
  }

  // Keep large UI assets out of internal RAM when PSRAM is available.
  if (data == nullptr && (!havePsram || bytes <= kLargeUiBufferBytes)) {
    data = static_cast<uint8_t *>(
        heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  }

  if (data == nullptr) {
    DiagnosticLog::printf("Dynamic UI: allocation failed: %s, %u bytes.\n",
                          purpose == nullptr ? "buffer" : purpose,
                          static_cast<unsigned>(bytes));
    logMemorySnapshot("UI allocation failure");
    return nullptr;
  }

  if (clear) memset(data, 0, bytes);
  return data;
}

void freeUiBuffer(uint8_t *data) {
  if (data != nullptr) heap_caps_free(data);
}

struct RuntimeWidget {
  lv_obj_t *object = nullptr;
  lv_obj_t *auxiliaryObject = nullptr;
  lv_obj_t *gradientSegments[kGradientArcSegments] = {};
  lv_point_t linePoints[2] = {};
  lv_img_dsc_t image = {};
  lv_img_dsc_t onImage = {};
  lv_img_dsc_t offImage = {};
  lv_img_dsc_t glyphAtlas = {};
  uint8_t *imageData = nullptr;
  uint8_t *onData = nullptr;
  uint8_t *offData = nullptr;
  uint8_t *glyphAtlasData = nullptr;
  // Normal image/state/font assets are shared through runtimeAssetCache.
  // Only GLYPHVAL's per-widget output mask is owned by RuntimeWidget.
  bool imageDataOwned = false;
  bool stateInitialized = false;
  bool currentState = false;
};

struct RuntimeAssetCacheEntry {
  uint8_t *data = nullptr;
  lv_img_dsc_t descriptor = {};
  bool loaded = false;
};

BmsUi::Package package;
RuntimeAssetCacheEntry runtimeAssetCache[BmsUi::MaxAssets] = {};

size_t packageAssetBytes() {
  size_t total = 0U;
  for (uint16_t i = 0U; i < package.assetCount; ++i) total += package.assets[i].byteCount;
  return total;
}
RuntimeWidget runtimeWidgets[BmsUi::MaxWidgets] = {};
lv_obj_t *customScreen = nullptr;
bool customActive = false;
bool packageWasValid = false;
bool connected = false;
BmsData latestData{};
bool haveLatestData = false;
bool latestPreview = false;
float latestPreviewRangeKm = 0.0f;
float latestPreviewPowerW = 0.0f;
char errorText[96] = "factory UI";

constexpr uint32_t kUiBootGuardMagic = 0x55494247U;  // "UIBG"
RTC_NOINIT_ATTR uint32_t uiBootGuardMagic;
RTC_NOINIT_ATTR uint32_t uiBootGuardInverse;
bool activationGuardArmed = false;

bool previousActivationWasInterrupted() {
  return uiBootGuardMagic == kUiBootGuardMagic &&
         uiBootGuardInverse == ~kUiBootGuardMagic;
}

void clearActivationGuard() {
  uiBootGuardMagic = 0U;
  uiBootGuardInverse = 0U;
  activationGuardArmed = false;
}

void armActivationGuard() {
  uiBootGuardMagic = kUiBootGuardMagic;
  uiBootGuardInverse = ~kUiBootGuardMagic;
  activationGuardArmed = true;
}


void releaseRuntimeWidgets() {
  for (uint16_t i = 0U; i < BmsUi::MaxWidgets; ++i) {
    if (runtimeWidgets[i].imageDataOwned && runtimeWidgets[i].imageData != nullptr) {
      freeUiBuffer(runtimeWidgets[i].imageData);
    }
    runtimeWidgets[i] = RuntimeWidget{};
  }
}

void releaseRuntimeAssets() {
  for (uint16_t i = 0U; i < BmsUi::MaxAssets; ++i) {
    if (runtimeAssetCache[i].data != nullptr) freeUiBuffer(runtimeAssetCache[i].data);
    runtimeAssetCache[i] = RuntimeAssetCacheEntry{};
  }
}

int runtimeAssetIndex(const char *name) {
  if (name == nullptr || name[0] == '\0') return -1;
  for (uint16_t i = 0U; i < package.assetCount; ++i) {
    if (strcmp(package.assets[i].name, name) == 0) return static_cast<int>(i);
  }
  return -1;
}

bool loadImageAsset(const char *name, lv_img_dsc_t &descriptor, uint8_t *&data) {
  const int index = runtimeAssetIndex(name);
  if (index < 0 || index >= static_cast<int>(BmsUi::MaxAssets)) return false;
  const BmsUi::AssetSpec &asset = package.assets[index];
  if (asset.byteCount == 0U || asset.byteCount > BmsUi::MaxRuntimeAssetBytes) return false;

  RuntimeAssetCacheEntry &cached = runtimeAssetCache[index];
  if (!cached.loaded) {
    cached.data = allocateUiBuffer(asset.byteCount, false, asset.name);
    if (cached.data == nullptr) {
      DiagnosticLog::printf("Dynamic UI: asset allocation failed for %s (%u bytes).\n",
                            asset.name,
                            static_cast<unsigned>(asset.byteCount));
      logMemorySnapshot("asset allocation failed");
      return false;
    }
    size_t loaded = 0U;
    if (!UiPackageStore::loadAsset(asset.name, cached.data, asset.byteCount, loaded) ||
        loaded != asset.byteCount) {
      freeUiBuffer(cached.data);
      cached = RuntimeAssetCacheEntry{};
      DiagnosticLog::printf("Dynamic UI: asset load failed for %s (%u/%u bytes).\n",
                            asset.name,
                            static_cast<unsigned>(loaded),
                            static_cast<unsigned>(asset.byteCount));
      return false;
    }

    memset(&cached.descriptor, 0, sizeof(cached.descriptor));
    if (asset.format == BmsUi::AssetFormat::Alpha8) cached.descriptor.header.cf = LV_IMG_CF_ALPHA_8BIT;
    else if (asset.format == BmsUi::AssetFormat::RGB565) cached.descriptor.header.cf = LV_IMG_CF_TRUE_COLOR;
    else cached.descriptor.header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA;
    cached.descriptor.header.always_zero = 0;
    cached.descriptor.header.reserved = 0;
    cached.descriptor.header.w = asset.width;
    cached.descriptor.header.h = asset.height;
    cached.descriptor.data_size = asset.byteCount;
    cached.descriptor.data = cached.data;
    cached.loaded = true;
    DiagnosticLog::printf("Dynamic UI: asset cached once: %s (%u bytes).\n",
                          asset.name,
                          static_cast<unsigned>(asset.byteCount));
  }

  descriptor = cached.descriptor;
  data = cached.data;
  return true;
}

bool loadStateAsset(const char *name, lv_img_dsc_t &descriptor, uint8_t *&data) {
  const BmsUi::AssetSpec *asset = BmsUi::findAsset(package, name);
  if (asset == nullptr || asset->byteCount > BmsUi::MaxRuntimeAssetBytes) return false;
  return loadImageAsset(name, descriptor, data);
}

lv_opa_t resolveOpacity(uint8_t percent) {
  if (percent == 0U) return LV_OPA_TRANSP;
  if (percent >= 100U) return LV_OPA_COVER;
  return static_cast<lv_opa_t>((static_cast<uint16_t>(percent) * 255U + 50U) / 100U);
}

const lv_font_t *resolveFont(BmsUi::FontId id) {
  switch (id) {
    case BmsUi::FontId::Montserrat14: return &lv_font_montserrat_14;
    case BmsUi::FontId::Montserrat16: return &lv_font_montserrat_16;
    case BmsUi::FontId::Montserrat22: return &lv_font_montserrat_22;
    case BmsUi::FontId::Montserrat32: return &lv_font_montserrat_32;
    case BmsUi::FontId::Montserrat48: return &lv_font_montserrat_48;
    case BmsUi::FontId::Chinese16: return &ui_font_found_cn_16;
    default: return &lv_font_montserrat_16;
  }
}

lv_text_align_t resolveAlign(BmsUi::AlignId id) {
  switch (id) {
    case BmsUi::AlignId::Left: return LV_TEXT_ALIGN_LEFT;
    case BmsUi::AlignId::Right: return LV_TEXT_ALIGN_RIGHT;
    case BmsUi::AlignId::Center:
    default: return LV_TEXT_ALIGN_CENTER;
  }
}

void configureCommonObject(lv_obj_t *object) {
  if (object == nullptr) return;
  lv_obj_clear_flag(object, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
}

bool isDegreeCelsiusSuffix(const char *suffix) {
  if (suffix == nullptr) return false;
  return strstr(suffix, "°C") != nullptr || strstr(suffix, "℃") != nullptr;
}

const char *deviceUnitText(const BmsUi::WidgetSpec &spec) {
  if (isDegreeCelsiusSuffix(spec.suffix)) return "℃";
  const char *unit = spec.suffix;
  while (unit != nullptr && *unit == ' ') ++unit;
  return unit == nullptr ? "" : unit;
}

const lv_font_t *resolveUnitFont(const BmsUi::WidgetSpec &spec) {
  if (isDegreeCelsiusSuffix(spec.suffix)) return &ui_font_found_cn_16;
  return resolveFont(spec.font);
}

void styleLabel(lv_obj_t *label, const BmsUi::WidgetSpec &spec, const lv_font_t *font, lv_text_align_t align) {
  lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
  lv_obj_set_style_bg_opa(label, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_border_width(label, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_pad_all(label, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_font(label, font, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_color(label, lv_color_hex(spec.color), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_opa(label, resolveOpacity(spec.opacity), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_align(label, align, LV_PART_MAIN | LV_STATE_DEFAULT);
  configureCommonObject(label);
}

lv_obj_t *createLabel(const BmsUi::WidgetSpec &spec, RuntimeWidget &runtime) {
  lv_obj_t *label = lv_label_create(customScreen);
  if (label == nullptr) return nullptr;

  const bool splitUnit = spec.binding != BmsUi::DataId::None &&
                         !BmsUi::isSwitchBinding(spec.binding) &&
                         spec.suffix[0] != '\0';
  if (!splitUnit) {
    lv_obj_set_pos(label, spec.x, spec.y);
    lv_obj_set_size(label, spec.width, spec.height);
    lv_label_set_text_static(label, "");
    const lv_text_align_t align = spec.binding != BmsUi::DataId::None &&
                                  !BmsUi::isSwitchBinding(spec.binding)
                                      ? LV_TEXT_ALIGN_RIGHT
                                      : resolveAlign(spec.align);
    styleLabel(label, spec, resolveFont(spec.font), align);
    return label;
  }

  lv_obj_t *unit = lv_label_create(customScreen);
  if (unit == nullptr) {
    lv_obj_del(label);
    return nullptr;
  }
  const char *unitText = deviceUnitText(spec);
  const lv_font_t *valueFont = resolveFont(spec.font);
  const lv_font_t *unitFont = resolveUnitFont(spec);
  lv_label_set_text(unit, unitText);
  lv_obj_set_width(unit, LV_SIZE_CONTENT);
  lv_obj_set_height(unit, spec.height);
  styleLabel(unit, spec, unitFont, LV_TEXT_ALIGN_RIGHT);
  lv_obj_update_layout(unit);

  lv_coord_t unitWidth = lv_obj_get_width(unit);
  const lv_coord_t gap = spec.font == BmsUi::FontId::Montserrat48 ? 5 :
                         spec.font == BmsUi::FontId::Montserrat32 ? 4 : 3;
  if (unitWidth < 1) unitWidth = 1;
  lv_coord_t valueWidth = static_cast<lv_coord_t>(spec.width) - unitWidth - gap;
  if (valueWidth < 1) valueWidth = 1;

  lv_obj_set_pos(label, spec.x, spec.y);
  lv_obj_set_size(label, valueWidth, spec.height);
  lv_label_set_text_static(label, "");
  styleLabel(label, spec, valueFont, LV_TEXT_ALIGN_RIGHT);

  lv_coord_t unitYOffset = 0;
  if (valueFont != nullptr && unitFont != nullptr && valueFont->line_height > unitFont->line_height) {
    unitYOffset = static_cast<lv_coord_t>((valueFont->line_height - unitFont->line_height) / 2);
  }
  lv_obj_set_pos(unit, spec.x + valueWidth + gap, spec.y + unitYOffset);
  lv_obj_set_height(unit, spec.height > unitYOffset ? spec.height - unitYOffset : 1);
  runtime.auxiliaryObject = unit;
  return label;
}

lv_obj_t *createRect(const BmsUi::WidgetSpec &spec) {
  lv_obj_t *rect = lv_obj_create(customScreen);
  lv_obj_remove_style_all(rect);
  lv_obj_set_pos(rect, spec.x, spec.y);
  lv_obj_set_size(rect, spec.width, spec.height);
  lv_obj_set_style_bg_color(rect, lv_color_hex(spec.color), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_opa(rect, resolveOpacity(spec.opacity), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_border_color(rect, lv_color_hex(spec.borderColor), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_border_width(rect, spec.borderWidth, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_border_opa(rect, resolveOpacity(spec.opacity), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_radius(rect, spec.radius, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_pad_all(rect, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
  configureCommonObject(rect);
  return rect;
}

lv_obj_t *createVectorShape(const BmsUi::WidgetSpec &spec) {
  lv_obj_t *shape = lv_obj_create(customScreen);
  if (shape == nullptr) return nullptr;
  lv_obj_remove_style_all(shape);
  lv_obj_set_pos(shape, spec.x, spec.y);
  lv_obj_set_size(shape, spec.width, spec.height);
  lv_obj_set_style_bg_color(shape, lv_color_hex(spec.color), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_opa(shape, spec.fill ? resolveOpacity(spec.opacity) : LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_border_color(shape, lv_color_hex(spec.borderColor), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_border_width(shape, spec.borderWidth, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_border_opa(shape, spec.borderWidth > 0U ? resolveOpacity(spec.opacity) : LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_coord_t radius = 0;
  if (spec.shapeKind == BmsUi::ShapeKind::Circle) {
    radius = LV_RADIUS_CIRCLE;
  } else if (spec.cornerKind == BmsUi::CornerKind::Round) {
    radius = spec.radius;
  }
  lv_obj_set_style_radius(shape, radius, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_pad_all(shape, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
  configureCommonObject(shape);
  return shape;
}

lv_obj_t *createLine(const BmsUi::WidgetSpec &spec, RuntimeWidget &runtime) {
  runtime.linePoints[0] = {static_cast<lv_coord_t>(spec.x), static_cast<lv_coord_t>(spec.y)};
  runtime.linePoints[1] = {static_cast<lv_coord_t>(spec.x2), static_cast<lv_coord_t>(spec.y2)};
  lv_obj_t *line = lv_line_create(customScreen);
  lv_line_set_points(line, runtime.linePoints, 2U);
  lv_obj_set_style_line_color(line, lv_color_hex(spec.color), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_line_width(line, spec.lineWidth, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_line_opa(line, resolveOpacity(spec.opacity), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_line_rounded(line, spec.rounded, LV_PART_MAIN | LV_STATE_DEFAULT);
  configureCommonObject(line);
  return line;
}

uint32_t interpolateRgb(uint32_t start, uint32_t end, uint16_t numerator, uint16_t denominator) {
  if (denominator == 0U) return start;
  const uint8_t sr = static_cast<uint8_t>((start >> 16U) & 0xFFU);
  const uint8_t sg = static_cast<uint8_t>((start >> 8U) & 0xFFU);
  const uint8_t sb = static_cast<uint8_t>(start & 0xFFU);
  const uint8_t er = static_cast<uint8_t>((end >> 16U) & 0xFFU);
  const uint8_t eg = static_cast<uint8_t>((end >> 8U) & 0xFFU);
  const uint8_t eb = static_cast<uint8_t>(end & 0xFFU);
  const auto mix = [numerator, denominator](uint8_t a, uint8_t b) -> uint8_t {
    const int32_t delta = static_cast<int32_t>(b) - static_cast<int32_t>(a);
    return static_cast<uint8_t>(static_cast<int32_t>(a) + (delta * numerator + denominator / 2U) / denominator);
  };
  return (static_cast<uint32_t>(mix(sr, er)) << 16U) |
         (static_cast<uint32_t>(mix(sg, eg)) << 8U) |
         static_cast<uint32_t>(mix(sb, eb));
}

int16_t normalizedAngle(int32_t angle) {
  angle %= 360;
  if (angle < 0) angle += 360;
  return static_cast<int16_t>(angle);
}

lv_obj_t *createArc(const BmsUi::WidgetSpec &spec, RuntimeWidget &runtime) {
  lv_obj_t *arc = lv_arc_create(customScreen);
  if (arc == nullptr) return nullptr;
  lv_obj_remove_style_all(arc);
  lv_obj_set_pos(arc, spec.x, spec.y);
  lv_obj_set_size(arc, spec.width, spec.height);
  lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_border_width(arc, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_pad_all(arc, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_arc_width(arc, spec.lineWidth, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_arc_width(arc, spec.lineWidth, LV_PART_INDICATOR | LV_STATE_DEFAULT);
  lv_obj_set_style_arc_color(arc, lv_color_hex(spec.secondaryColor), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_arc_color(arc, lv_color_hex(spec.color), LV_PART_INDICATOR | LV_STATE_DEFAULT);
  lv_obj_set_style_arc_opa(arc, resolveOpacity(spec.opacity), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_arc_opa(arc, spec.gradientEnabled ? LV_OPA_TRANSP : resolveOpacity(spec.opacity), LV_PART_INDICATOR | LV_STATE_DEFAULT);
  lv_obj_set_style_arc_rounded(arc, spec.rounded, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_arc_rounded(arc, spec.rounded, LV_PART_INDICATOR | LV_STATE_DEFAULT);
  lv_obj_remove_style(arc, nullptr, LV_PART_KNOB);
  lv_arc_set_mode(arc, LV_ARC_MODE_NORMAL);
  lv_arc_set_range(arc, 0, 1000);
  lv_arc_set_bg_angles(arc, normalizedAngle(spec.startAngle), normalizedAngle(spec.endAngle));
  lv_arc_set_value(arc, 0);
  configureCommonObject(arc);

  if (spec.gradientEnabled) {
    for (uint8_t i = 0U; i < kGradientArcSegments; ++i) {
      lv_obj_t *segment = lv_arc_create(customScreen);
      if (segment == nullptr) return arc;
      lv_obj_remove_style_all(segment);
      lv_obj_set_pos(segment, spec.x, spec.y);
      lv_obj_set_size(segment, spec.width, spec.height);
      lv_obj_set_style_bg_opa(segment, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
      lv_obj_set_style_border_width(segment, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
      lv_obj_set_style_pad_all(segment, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
      lv_obj_set_style_arc_width(segment, spec.lineWidth, LV_PART_MAIN | LV_STATE_DEFAULT);
      const uint16_t colorPos = static_cast<uint16_t>(i * 1000U / (kGradientArcSegments - 1U));
      lv_obj_set_style_arc_color(segment, lv_color_hex(interpolateRgb(spec.color, spec.gradientColor, colorPos, 1000U)), LV_PART_MAIN | LV_STATE_DEFAULT);
      lv_obj_set_style_arc_opa(segment, resolveOpacity(spec.opacity), LV_PART_MAIN | LV_STATE_DEFAULT);
      lv_obj_set_style_arc_opa(segment, LV_OPA_TRANSP, LV_PART_INDICATOR | LV_STATE_DEFAULT);
      lv_obj_set_style_arc_rounded(segment, false, LV_PART_MAIN | LV_STATE_DEFAULT);
      lv_obj_remove_style(segment, nullptr, LV_PART_KNOB);
      lv_arc_set_bg_angles(segment, 0, 1);
      configureCommonObject(segment);
      lv_obj_add_flag(segment, LV_OBJ_FLAG_HIDDEN);
      runtime.gradientSegments[i] = segment;
    }
  }
  return arc;
}

void updateGradientArc(const BmsUi::WidgetSpec &spec, RuntimeWidget &runtime, int32_t normalized) {
  if (!spec.gradientEnabled) {
    lv_arc_set_value(runtime.object, normalized);
    return;
  }
  if (normalized < 0) normalized = 0;
  if (normalized > 1000) normalized = 1000;
  const int32_t totalSweep = spec.endAngle - spec.startAngle;
  const int32_t activeSweep = (totalSweep * normalized + 500) / 1000;
  int lastVisible = -1;
  for (uint8_t i = 0U; i < kGradientArcSegments; ++i) {
    lv_obj_t *segment = runtime.gradientSegments[i];
    if (segment == nullptr) continue;
    const int32_t segStartOffset = (totalSweep * i) / kGradientArcSegments;
    const int32_t segEndOffset = (totalSweep * (i + 1U)) / kGradientArcSegments;
    if (activeSweep <= segStartOffset) {
      lv_obj_add_flag(segment, LV_OBJ_FLAG_HIDDEN);
      continue;
    }
    const int32_t shownEnd = activeSweep < segEndOffset ? activeSweep : segEndOffset;
    lv_arc_set_bg_angles(segment,
                         normalizedAngle(spec.startAngle + segStartOffset),
                         normalizedAngle(spec.startAngle + shownEnd));
    lv_obj_set_style_arc_rounded(segment, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(segment, LV_OBJ_FLAG_HIDDEN);
    lastVisible = i;
  }
  if (spec.rounded && lastVisible >= 0) {
    if (runtime.gradientSegments[0] != nullptr && !lv_obj_has_flag(runtime.gradientSegments[0], LV_OBJ_FLAG_HIDDEN)) {
      lv_obj_set_style_arc_rounded(runtime.gradientSegments[0], true, LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    if (runtime.gradientSegments[lastVisible] != nullptr) {
      lv_obj_set_style_arc_rounded(runtime.gradientSegments[lastVisible], true, LV_PART_MAIN | LV_STATE_DEFAULT);
    }
  }
}

lv_coord_t effectiveBarRadius(const BmsUi::WidgetSpec &spec, lv_coord_t width, lv_coord_t height) {
  if (!spec.rounded) return 0;
  const lv_coord_t limit = static_cast<lv_coord_t>((width < height ? width : height) / 2);
  if (limit <= 0) return 0;
  const lv_coord_t requested = spec.radius > 0 ? static_cast<lv_coord_t>(spec.radius) : limit;
  return requested < limit ? requested : limit;
}

lv_obj_t *createBar(const BmsUi::WidgetSpec &spec, RuntimeWidget &runtime) {
  lv_obj_t *track = lv_obj_create(customScreen);
  if (track == nullptr) return nullptr;
  lv_obj_remove_style_all(track);
  lv_obj_set_pos(track, spec.x, spec.y);
  lv_obj_set_size(track, spec.width, spec.height);
  lv_obj_set_style_bg_color(track, lv_color_hex(spec.secondaryColor), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_opa(track, resolveOpacity(spec.opacity), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_radius(track, effectiveBarRadius(spec, spec.width, spec.height), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_clear_flag(track, LV_OBJ_FLAG_SCROLLABLE);
  configureCommonObject(track);

  lv_obj_t *indicator = lv_obj_create(track);
  if (indicator == nullptr) {
    lv_obj_del(track);
    return nullptr;
  }
  lv_obj_remove_style_all(indicator);
  lv_obj_set_pos(indicator, 0, 0);
  lv_obj_set_size(indicator, 1, spec.height);
  lv_obj_set_style_bg_color(indicator, lv_color_hex(spec.color), LV_PART_MAIN | LV_STATE_DEFAULT);
  if (spec.gradientEnabled) {
    BmsUi::GradientDirection direction = spec.gradientDirection;
    if (direction == BmsUi::GradientDirection::Auto) {
      direction = spec.width < spec.height ? BmsUi::GradientDirection::Vertical : BmsUi::GradientDirection::Horizontal;
    }
    uint32_t startColor = spec.color;
    uint32_t endColor = spec.gradientColor;
    if (direction == BmsUi::GradientDirection::Vertical && spec.width < spec.height) {
      startColor = spec.gradientColor;
      endColor = spec.color;
      lv_obj_set_style_bg_color(indicator, lv_color_hex(startColor), LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    lv_obj_set_style_bg_grad_color(indicator, lv_color_hex(endColor), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(indicator, direction == BmsUi::GradientDirection::Vertical ? LV_GRAD_DIR_VER : LV_GRAD_DIR_HOR, LV_PART_MAIN | LV_STATE_DEFAULT);
  } else {
    lv_obj_set_style_bg_grad_dir(indicator, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
  }
  lv_obj_set_style_bg_opa(indicator, resolveOpacity(spec.opacity), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_radius(indicator, effectiveBarRadius(spec, 1, spec.height), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_clear_flag(indicator, LV_OBJ_FLAG_SCROLLABLE);
  configureCommonObject(indicator);
  lv_obj_add_flag(indicator, LV_OBJ_FLAG_HIDDEN);

  runtime.auxiliaryObject = indicator;
  return track;
}

void updateBar(const BmsUi::WidgetSpec &spec, RuntimeWidget &runtime, int32_t normalized) {
  lv_obj_t *indicator = runtime.auxiliaryObject;
  if (indicator == nullptr) return;

  if (normalized < 0) normalized = 0;
  if (normalized > 1000) normalized = 1000;
  if (normalized == 0) {
    lv_obj_add_flag(indicator, LV_OBJ_FLAG_HIDDEN);
    return;
  }

  lv_obj_clear_flag(indicator, LV_OBJ_FLAG_HIDDEN);
  if (spec.width < spec.height) {
    lv_coord_t fillHeight = static_cast<lv_coord_t>((static_cast<int32_t>(spec.height) * normalized + 500) / 1000);
    if (fillHeight < 1) fillHeight = 1;
    if (fillHeight > spec.height) fillHeight = spec.height;
    lv_obj_set_pos(indicator, 0, spec.height - fillHeight);
    lv_obj_set_size(indicator, spec.width, fillHeight);
    lv_obj_set_style_radius(indicator, effectiveBarRadius(spec, spec.width, fillHeight), LV_PART_MAIN | LV_STATE_DEFAULT);
  } else {
    lv_coord_t fillWidth = static_cast<lv_coord_t>((static_cast<int32_t>(spec.width) * normalized + 500) / 1000);
    if (fillWidth < 1) fillWidth = 1;
    if (fillWidth > spec.width) fillWidth = spec.width;
    lv_obj_set_pos(indicator, 0, 0);
    lv_obj_set_size(indicator, fillWidth, spec.height);
    lv_obj_set_style_radius(indicator, effectiveBarRadius(spec, fillWidth, spec.height), LV_PART_MAIN | LV_STATE_DEFAULT);
  }
}

lv_obj_t *createImageWidget(const BmsUi::WidgetSpec &spec, RuntimeWidget &runtime) {
  if (!loadImageAsset(spec.asset, runtime.image, runtime.imageData)) return nullptr;
  lv_obj_t *image = lv_img_create(customScreen);
  if (image == nullptr) return nullptr;
  lv_obj_set_pos(image, spec.x, spec.y);
  lv_obj_set_size(image, spec.width, spec.height);
  lv_img_set_src(image, &runtime.image);
  if (spec.type == BmsUi::WidgetType::TextImage) {
    lv_obj_set_style_img_recolor(image, lv_color_hex(spec.color), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_img_recolor_opa(image, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
  }
  lv_obj_set_style_img_opa(image, resolveOpacity(spec.opacity), LV_PART_MAIN | LV_STATE_DEFAULT);
  configureCommonObject(image);
  return image;
}


uint32_t nextUtf8Codepoint(const char *&cursor) {
  if (cursor == nullptr || *cursor == '\0') return 0U;
  const uint8_t c0 = static_cast<uint8_t>(*cursor++);
  if (c0 < 0x80U) return c0;
  if ((c0 & 0xE0U) == 0xC0U && cursor[0] != '\0') {
    const uint8_t c1 = static_cast<uint8_t>(*cursor++);
    return ((c0 & 0x1FU) << 6U) | (c1 & 0x3FU);
  }
  if ((c0 & 0xF0U) == 0xE0U && cursor[0] != '\0' && cursor[1] != '\0') {
    const uint8_t c1 = static_cast<uint8_t>(*cursor++);
    const uint8_t c2 = static_cast<uint8_t>(*cursor++);
    return ((c0 & 0x0FU) << 12U) | ((c1 & 0x3FU) << 6U) | (c2 & 0x3FU);
  }
  if ((c0 & 0xF8U) == 0xF0U && cursor[0] != '\0' && cursor[1] != '\0' && cursor[2] != '\0') {
    const uint8_t c1 = static_cast<uint8_t>(*cursor++);
    const uint8_t c2 = static_cast<uint8_t>(*cursor++);
    const uint8_t c3 = static_cast<uint8_t>(*cursor++);
    return ((c0 & 0x07U) << 18U) | ((c1 & 0x3FU) << 12U) | ((c2 & 0x3FU) << 6U) | (c3 & 0x3FU);
  }
  return 0xFFFDU;
}

int glyphIndexForCodepoint(const BmsUi::WidgetSpec &spec, uint32_t codepoint) {
  for (uint8_t i = 0U; i < spec.glyphCount; ++i) {
    if (spec.glyphCodepoints[i] == codepoint) return static_cast<int>(i);
  }
  return -1;
}

int16_t glyphTextAdvance(const BmsUi::WidgetSpec &spec, const char *text) {
  if (text == nullptr) return 0;
  int16_t total = 0;
  const char *cursor = text;
  while (*cursor != '\0') {
    const uint32_t cp = nextUtf8Codepoint(cursor);
    const int index = glyphIndexForCodepoint(spec, cp);
    if (index >= 0) total = static_cast<int16_t>(total + spec.glyphAdvances[index]);
  }
  return total;
}

void blitGlyphString(const BmsUi::WidgetSpec &spec,
                     RuntimeWidget &runtime,
                     const char *text,
                     int16_t startX) {
  if (text == nullptr || runtime.imageData == nullptr || runtime.glyphAtlasData == nullptr) return;
  const BmsUi::AssetSpec *atlas = BmsUi::findAsset(package, spec.asset);
  if (atlas == nullptr || atlas->format != BmsUi::AssetFormat::Alpha8) return;
  int16_t x = startX;
  const char *cursor = text;
  while (*cursor != '\0') {
    const uint32_t cp = nextUtf8Codepoint(cursor);
    const int index = glyphIndexForCodepoint(spec, cp);
    if (index < 0) continue;
    const uint16_t cellX = static_cast<uint16_t>(index % spec.glyphColumns) * spec.glyphCellWidth;
    const uint16_t cellY = static_cast<uint16_t>(index / spec.glyphColumns) * static_cast<uint16_t>(spec.height);
    for (int16_t y = 0; y < spec.height; ++y) {
      const size_t srcRow = static_cast<size_t>(cellY + y) * atlas->width + cellX;
      const size_t dstRow = static_cast<size_t>(y) * spec.width;
      for (uint8_t gx = 0U; gx < spec.glyphCellWidth; ++gx) {
        const int16_t dx = static_cast<int16_t>(x + gx);
        if (dx < 0 || dx >= spec.width) continue;
        const uint8_t alpha = runtime.glyphAtlasData[srcRow + gx];
        uint8_t &dst = runtime.imageData[dstRow + static_cast<size_t>(dx)];
        if (alpha > dst) dst = alpha;
      }
    }
    x = static_cast<int16_t>(x + spec.glyphAdvances[index]);
  }
}

void renderGlyphValue(const BmsUi::WidgetSpec &spec,
                      RuntimeWidget &runtime,
                      const UiDataProvider::Context &context) {
  if (runtime.imageData == nullptr || runtime.object == nullptr) return;
  memset(runtime.imageData, 0, static_cast<size_t>(spec.width) * spec.height);

  char leftText[112] = {};
  UiDataProvider::formatLabelValue(spec, context, leftText, sizeof(leftText));

  const char *unitText = spec.suffix;
  while (unitText != nullptr && *unitText == ' ') ++unitText;
  if (unitText != nullptr && *unitText != '\0') {
    const int16_t suffixWidth = glyphTextAdvance(spec, unitText);
    const int16_t gap = static_cast<int16_t>(spec.glyphFontSize / 5U < 2U ? 2U : spec.glyphFontSize / 5U);
    const int16_t suffixStart = static_cast<int16_t>(spec.width - suffixWidth);
    const int16_t leftEnd = static_cast<int16_t>(suffixStart - gap);
    const int16_t leftWidth = glyphTextAdvance(spec, leftText);
    blitGlyphString(spec, runtime, leftText, static_cast<int16_t>(leftEnd - leftWidth));
    blitGlyphString(spec, runtime, unitText, suffixStart);
  } else {
    char text[112] = {};
    UiDataProvider::formatLabel(spec, context, text, sizeof(text));
    const int16_t width = glyphTextAdvance(spec, text);
    int16_t start = 0;
    if (spec.align == BmsUi::AlignId::Center) start = static_cast<int16_t>((spec.width - width) / 2);
    else if (spec.align == BmsUi::AlignId::Right) start = static_cast<int16_t>(spec.width - width);
    blitGlyphString(spec, runtime, text, start);
  }
  lv_obj_invalidate(runtime.object);
}

lv_obj_t *createGlyphValue(const BmsUi::WidgetSpec &spec, RuntimeWidget &runtime) {
  if (!loadImageAsset(spec.asset, runtime.glyphAtlas, runtime.glyphAtlasData)) return nullptr;
  const size_t bytes = static_cast<size_t>(spec.width) * spec.height;
  runtime.imageData = allocateUiBuffer(bytes, true, "glyph value output mask");
  if (runtime.imageData == nullptr) return nullptr;
  runtime.imageDataOwned = true;
  memset(&runtime.image, 0, sizeof(runtime.image));
  runtime.image.header.cf = LV_IMG_CF_ALPHA_8BIT;
  runtime.image.header.always_zero = 0;
  runtime.image.header.reserved = 0;
  runtime.image.header.w = static_cast<uint32_t>(spec.width);
  runtime.image.header.h = static_cast<uint32_t>(spec.height);
  runtime.image.data_size = bytes;
  runtime.image.data = runtime.imageData;
  lv_obj_t *image = lv_img_create(customScreen);
  if (image == nullptr) return nullptr;
  lv_obj_set_pos(image, spec.x, spec.y);
  lv_obj_set_size(image, spec.width, spec.height);
  lv_img_set_src(image, &runtime.image);
  lv_obj_set_style_img_recolor(image, lv_color_hex(spec.color), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_img_recolor_opa(image, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_img_opa(image, resolveOpacity(spec.opacity), LV_PART_MAIN | LV_STATE_DEFAULT);
  configureCommonObject(image);
  return image;
}

bool stateAssetsAreAlphaMasks(const BmsUi::WidgetSpec &spec) {
  const BmsUi::AssetSpec *on = BmsUi::findAsset(package, spec.onAsset);
  const BmsUi::AssetSpec *off = BmsUi::findAsset(package, spec.offAsset);
  return on != nullptr && off != nullptr && on->format == BmsUi::AssetFormat::Alpha8 && off->format == BmsUi::AssetFormat::Alpha8;
}

void applyStateImageTint(const BmsUi::WidgetSpec &spec, lv_obj_t *image, bool on) {
  if (image == nullptr || !stateAssetsAreAlphaMasks(spec)) return;
  lv_obj_set_style_img_recolor(image, lv_color_hex(on ? spec.color : spec.secondaryColor), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_img_recolor_opa(image, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
}

lv_obj_t *createStateImage(const BmsUi::WidgetSpec &spec, RuntimeWidget &runtime) {
  if (!loadStateAsset(spec.onAsset, runtime.onImage, runtime.onData)) return nullptr;
  if (!loadStateAsset(spec.offAsset, runtime.offImage, runtime.offData)) {
    runtime.onData = nullptr;
    return nullptr;
  }
  lv_obj_t *image = lv_img_create(customScreen);
  lv_obj_set_pos(image, spec.x, spec.y);
  lv_obj_set_size(image, spec.width, spec.height);
  lv_img_set_src(image, &runtime.offImage);
  applyStateImageTint(spec, image, false);
  lv_obj_set_style_img_opa(image, resolveOpacity(spec.opacity), LV_PART_MAIN | LV_STATE_DEFAULT);
  configureCommonObject(image);
  runtime.stateInitialized = true;
  runtime.currentState = false;
  return image;
}

int32_t normalizedValue(const BmsUi::WidgetSpec &spec, const UiDataProvider::Context &context) {
  float ratio = 0.0f;
  if (UiDataProvider::progressRatio(spec.binding, context, ratio)) {
    return static_cast<int32_t>(lroundf(ratio * 1000.0f));
  }

  float value = 0.0f;
  if (!UiDataProvider::numericValue(spec.binding, context, value)) return 0;
  if (!isfinite(value) || spec.maxValue <= spec.minValue) return 0;
  ratio = (value - spec.minValue) / (spec.maxValue - spec.minValue);
  if (ratio < 0.0f) ratio = 0.0f;
  if (ratio > 1.0f) ratio = 1.0f;
  return static_cast<int32_t>(lroundf(ratio * 1000.0f));
}

UiDataProvider::Context currentContext() {
  UiDataProvider::Context context;
  context.data = haveLatestData ? &latestData : nullptr;
  context.connected = connected;
  context.usePreviewOverrides = latestPreview;
  context.previewRangeKm = latestPreviewRangeKm;
  context.previewPowerW = latestPreviewPowerW;
  return context;
}

void render() {
  if (!customActive) return;
  const UiDataProvider::Context context = currentContext();
  char text[112] = {};

  for (uint16_t i = 0U; i < package.widgetCount; ++i) {
    const BmsUi::WidgetSpec &spec = package.widgets[i];
    lv_obj_t *object = runtimeWidgets[i].object;
    if (object == nullptr) continue;

    switch (spec.type) {
      case BmsUi::WidgetType::Label:
        if (runtimeWidgets[i].auxiliaryObject != nullptr) {
          UiDataProvider::formatLabelValue(spec, context, text, sizeof(text));
        } else {
          UiDataProvider::formatLabel(spec, context, text, sizeof(text));
        }
        lv_label_set_text(object, text);
        break;
      case BmsUi::WidgetType::GlyphValue:
        renderGlyphValue(spec, runtimeWidgets[i], context);
        break;
      case BmsUi::WidgetType::Arc:
        updateGradientArc(spec, runtimeWidgets[i], normalizedValue(spec, context));
        break;
      case BmsUi::WidgetType::Bar:
        updateBar(spec, runtimeWidgets[i], normalizedValue(spec, context));
        break;
      case BmsUi::WidgetType::StateImage: {
        float value = 0.0f;
        const bool on = UiDataProvider::numericValue(spec.binding, context, value) && value >= 0.5f;
        RuntimeWidget &runtime = runtimeWidgets[i];
        if (!runtime.stateInitialized || runtime.currentState != on) {
          lv_img_set_src(object, on ? &runtime.onImage : &runtime.offImage);
          applyStateImageTint(spec, object, on);
          runtime.currentState = on; runtime.stateInitialized = true;
        }
        break;
      }
      case BmsUi::WidgetType::Rect:
      case BmsUi::WidgetType::Line:
      case BmsUi::WidgetType::Image:
      case BmsUi::WidgetType::TextImage:
      case BmsUi::WidgetType::Shape:
      case BmsUi::WidgetType::VectorShape:
      default:
        break;
    }
  }
}

bool buildScreen() {
  logMemorySnapshot("before LVGL custom screen");
  customScreen = lv_obj_create(nullptr);
  if (customScreen == nullptr) {
    snprintf(errorText, sizeof(errorText), "LVGL screen allocation failed");
    return false;
  }

  lv_obj_set_pos(customScreen, 0, 0);
  lv_obj_set_size(customScreen, package.width, package.height);
  lv_obj_clear_flag(customScreen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scrollbar_mode(customScreen, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_style_bg_color(customScreen, lv_color_hex(package.backgroundColor), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_opa(customScreen, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_border_width(customScreen, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_radius(customScreen, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_pad_all(customScreen, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

  releaseRuntimeWidgets();
  releaseRuntimeAssets();
  for (uint16_t i = 0U; i < package.widgetCount; ++i) {
    const BmsUi::WidgetSpec &spec = package.widgets[i];
    RuntimeWidget &runtime = runtimeWidgets[i];
    switch (spec.type) {
      case BmsUi::WidgetType::Label: runtime.object = createLabel(spec, runtime); break;
      case BmsUi::WidgetType::Rect: runtime.object = createRect(spec); break;
      case BmsUi::WidgetType::Line: runtime.object = createLine(spec, runtime); break;
      case BmsUi::WidgetType::Arc: runtime.object = createArc(spec, runtime); break;
      case BmsUi::WidgetType::Bar: runtime.object = createBar(spec, runtime); break;
      case BmsUi::WidgetType::StateImage: runtime.object = createStateImage(spec, runtime); break;
      case BmsUi::WidgetType::Image:
      case BmsUi::WidgetType::TextImage:
      case BmsUi::WidgetType::Shape: runtime.object = createImageWidget(spec, runtime); break;
      case BmsUi::WidgetType::VectorShape: runtime.object = createVectorShape(spec); break;
      case BmsUi::WidgetType::GlyphValue: runtime.object = createGlyphValue(spec, runtime); break;
      default: runtime.object = nullptr; break;
    }
    if (runtime.object == nullptr) {
      snprintf(errorText, sizeof(errorText), "LVGL allocation failed at widget %u", static_cast<unsigned>(i));
      DiagnosticLog::printf("Dynamic UI: widget %u creation failed (type=%u, id=%s).\n",
                            static_cast<unsigned>(i),
                            static_cast<unsigned>(spec.type),
                            spec.id);
      logMemorySnapshot("widget creation failed");
      lv_obj_del(customScreen);
      customScreen = nullptr;
      releaseRuntimeWidgets();
      releaseRuntimeAssets();
      return false;
    }
    if ((i & 0x07U) == 0x07U) {
      logMemorySnapshot("building widgets");
#if defined(ARDUINO)
      delay(0);
#endif
    }
  }
  logMemorySnapshot("after LVGL custom screen");
  return true;
}

}  // namespace

bool begin() {
  customActive = false;
  packageWasValid = false;
  activationGuardArmed = false;
  snprintf(errorText, sizeof(errorText), "factory UI");

  logPsramConfiguration();
  logMemorySnapshot("dynamic UI begin");

  if (!UiPackageStore::begin()) {
    DiagnosticLog::printf("Dynamic UI storage unavailable: %s\n",
                          UiPackageStore::lastStorageError());
    return false;
  }

  // Large UI resources are PSRAM-first.  Do not hard-code a board marketing
  // capacity here: some boards expose 8 MiB while others expose 16 MiB.
  // What matters for runtime safety is whether a usable external-RAM heap is
  // actually present.  Resource-level allocation checks below remain the
  // final authority for large images/fonts.  If PSRAM is absent, defer before
  // arming the crash guard so the uploaded package is preserved.
  const size_t detectedPsram = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
  if (!psramUsable()) {
    snprintf(errorText, sizeof(errorText),
             "PSRAM unavailable/too small (%u bytes); custom UI kept",
             static_cast<unsigned>(detectedPsram));
    DiagnosticLog::printf(
        "Dynamic UI deferred: %s. Enable PSRAM and reboot; package was NOT quarantined.\n",
        errorText);
    logMemorySnapshot("custom UI deferred: PSRAM unavailable");
    return false;
  }

  if (previousActivationWasInterrupted()) {
    const bool quarantined = UiPackageStore::quarantineCurrentPackage();
    if (quarantined) clearActivationGuard();
    snprintf(errorText,
             sizeof(errorText),
             quarantined
                 ? "previous custom UI crashed during activation; disabled"
                 : "previous custom UI activation crashed; disable failed");
    DiagnosticLog::printf("Dynamic UI disabled: %s\n", errorText);
    return false;
  }

  if (!UiPackageStore::hasCustomPackage()) {
    DiagnosticLog::write("Dynamic UI: factory UI.\n");
    return false;
  }

  DiagnosticLog::write("Dynamic UI: validating custom UI.\n");

  armActivationGuard();

  BmsUi::ParseResult result;
  if (!UiPackageStore::load(package, result)) {
    snprintf(errorText, sizeof(errorText), "line %u: %s",
             static_cast<unsigned>(result.line), result.message);
    DiagnosticLog::printf("Dynamic UI rejected: %s\n", errorText);
    clearActivationGuard();
    return false;
  }

  packageWasValid = true;
  logMemorySnapshot("package parsed");

  const size_t assetBytes = packageAssetBytes();
  const size_t runtimeLimit = availableRuntimeAssetLimit();
  if (assetBytes > runtimeLimit) {
    snprintf(errorText, sizeof(errorText), "asset bytes %u exceed runtime limit %u",
             static_cast<unsigned>(assetBytes), static_cast<unsigned>(runtimeLimit));
    DiagnosticLog::printf("Dynamic UI rejected: %s\n", errorText);
    clearActivationGuard();
    return false;
  }

  DiagnosticLog::printf("Dynamic UI: package resources=%u bytes, runtime limit=%u bytes.\n",
                        static_cast<unsigned>(assetBytes),
                        static_cast<unsigned>(runtimeLimit));
  logMemorySnapshot("before building custom UI");

  if (!buildScreen()) {
    DiagnosticLog::printf("Dynamic UI build failed: %s\n", errorText);
    clearActivationGuard();
    return false;
  }

  customActive = true;
  snprintf(errorText, sizeof(errorText), "activation pending first frame");
  DiagnosticLog::printf("Dynamic UI active: '%s', %u widgets.\n",
                        package.title,
                        static_cast<unsigned>(package.widgetCount));
  render();
  logMemorySnapshot("after first dynamic UI render");
  return true;
}

void markBootHealthy() {
  if (!activationGuardArmed) return;
  clearActivationGuard();
  snprintf(errorText, sizeof(errorText), "ok");
  DiagnosticLog::write("Dynamic UI: first-frame activation completed; boot guard cleared.\n");
  logMemorySnapshot("dynamic UI boot healthy");
}

void loop(uint32_t nowMs) {
  (void)nowMs;
}

void update(const BmsData &data) {
  latestData = data;
  haveLatestData = true;
  latestPreview = false;
  render();
}

void updatePreview(const BmsData &data, float previewRangeKm, float previewPowerW) {
  latestData = data;
  haveLatestData = true;
  latestPreview = true;
  latestPreviewRangeKm = previewRangeKm;
  latestPreviewPowerW = previewPowerW;
  render();
}

void setConnected(bool value) {
  connected = value;
  render();
}

bool active() {
  return customActive && customScreen != nullptr;
}

bool packageValid() {
  return packageWasValid;
}

lv_obj_t *screen() {
  return active() ? customScreen : nullptr;
}

const char *lastError() {
  return errorText;
}

uint16_t widgetCount() {
  return active() ? package.widgetCount : 0U;
}

const char *title() {
  return active() && package.title[0] != '\0'
             ? package.title
             : "Factory UI";
}

}  // namespace DynamicUi
