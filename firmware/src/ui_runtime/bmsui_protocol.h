#pragma once

#include <Arduino.h>

#include "../app_config.h"

namespace BmsUi {

constexpr uint16_t ProtocolVersion = 7;
constexpr uint16_t PreviousProtocolVersion = 6;
constexpr uint16_t LegacyProtocolVersion = 1;
constexpr uint16_t MaxWidgets = 48;
constexpr uint16_t MaxAssets = 48;
constexpr size_t MaxLineLength = 1024;
constexpr uint8_t MaxGlyphs = 48;
constexpr size_t MaxPackageBytes = 294912;
constexpr size_t MaxIdLength = 20;
constexpr size_t MaxAssetNameLength = 31;
constexpr size_t MaxTextLength = 160;
constexpr size_t MaxAffixLength = 40;
constexpr size_t MaxTitleLength = 63;
constexpr uint16_t MaxStateIconDimension = 64;
constexpr size_t MaxStateIconBytes = static_cast<size_t>(MaxStateIconDimension) * MaxStateIconDimension * 3U;
constexpr size_t BaseRuntimeAssetBytes = 98304;
constexpr size_t MaxRuntimeAssetBytes = 196608;

constexpr char CurrentPath[] = "/ui.bmsui";
constexpr char StagingPath[] = "/ui.tmp";
constexpr char BackupPath[] = "/ui.bak";
constexpr char RejectedPath[] = "/ui.bad";

enum class AssetFormat : uint8_t {
  RGB565A8,
  RGB565,
  Alpha8,
};

enum class GradientDirection : uint8_t {
  Auto,
  Horizontal,
  Vertical,
};

enum class WidgetType : uint8_t {
  Label,
  Rect,
  Line,
  Arc,
  Bar,
  StateImage,
  Image,
  TextImage,
  Shape,
  VectorShape,
  GlyphValue,
};

enum class ShapeKind : uint8_t {
  Rect,
  Circle,
};

enum class CornerKind : uint8_t {
  Square,
  Round,
};

enum class FontId : uint8_t {
  Montserrat14,
  Montserrat16,
  Montserrat22,
  Montserrat32,
  Montserrat48,
  Chinese16,
};

enum class AlignId : uint8_t {
  Left,
  Center,
  Right,
};

enum class DataId : uint8_t {
  None,
  Soc,
  Soh,  // legacy
  TotalVoltage,
  Current,
  Power,
  RemainingCapacityAh,
  TotalCapacityAh,
  RemainingRangeKm,
  MosTemperature,
  DeltaCellVoltage,
  MaxCellVoltage,  // legacy
  MinCellVoltage,  // legacy
  AverageCellVoltage,
  CycleCount,
  CellCount,       // legacy
  MaxCellIndex,    // legacy
  MinCellIndex,    // legacy
  ChargeMos,
  DischargeMos,
  BalancerStatus,
  Connected,
};

struct AssetSpec {
  char name[MaxAssetNameLength + 1] = {};
  uint16_t width = 0;
  uint16_t height = 0;
  AssetFormat format = AssetFormat::RGB565A8;
  uint32_t byteCount = 0;
  uint32_t expectedCrc32 = 0;
  uint32_t receivedBytes = 0;
  uint32_t runningCrc32 = 0xFFFFFFFFU;
};

struct WidgetSpec {
  WidgetType type = WidgetType::Label;
  char id[MaxIdLength + 1] = {};

  int16_t x = 0;
  int16_t y = 0;
  int16_t width = 1;
  int16_t height = 1;

  uint32_t color = 0xFFFFFF;
  uint32_t secondaryColor = 0x202020;
  uint32_t borderColor = 0x000000;
  uint32_t gradientColor = 0xFFFFFF;
  bool gradientEnabled = false;
  GradientDirection gradientDirection = GradientDirection::Auto;

  uint8_t lineWidth = 1;
  uint8_t borderWidth = 0;
  uint8_t radius = 0;
  uint8_t opacity = 100;
  bool rounded = true;
  FontId font = FontId::Montserrat16;
  AlignId align = AlignId::Center;
  DataId binding = DataId::None;
  uint8_t decimals = 0;

  float minValue = 0.0f;
  float maxValue = 100.0f;
  int16_t startAngle = 135;
  int16_t endAngle = 405;

  int16_t x2 = 0;
  int16_t y2 = 0;

  char prefix[MaxAffixLength + 1] = {};
  char suffix[MaxAffixLength + 1] = {};
  char text[MaxTextLength + 1] = {};

  char asset[MaxAssetNameLength + 1] = {};
  char onAsset[MaxAssetNameLength + 1] = {};
  char offAsset[MaxAssetNameLength + 1] = {};

  ShapeKind shapeKind = ShapeKind::Rect;
  CornerKind cornerKind = CornerKind::Square;
  bool fill = true;

  uint8_t glyphCount = 0;
  uint8_t glyphCellWidth = 0;
  uint8_t glyphColumns = 0;
  uint8_t glyphFontSize = 0;
  bool glyphBold = false;
  uint32_t glyphCodepoints[MaxGlyphs] = {};
  uint8_t glyphAdvances[MaxGlyphs] = {};
};

struct Package {
  uint16_t version = ProtocolVersion;
  uint16_t width = AppConfig::Display::Width;
  uint16_t height = AppConfig::Display::Height;
  uint32_t backgroundColor = 0x000000;
  char title[MaxTitleLength + 1] = {};
  uint16_t widgetCount = 0;
  uint16_t assetCount = 0;
  WidgetSpec widgets[MaxWidgets] = {};
  AssetSpec assets[MaxAssets] = {};
};

struct ParseResult {
  bool ok = false;
  uint16_t line = 0;
  char message[112] = {};
};

const char *widgetTypeName(WidgetType type);
const char *bindingName(DataId id);
bool isSwitchBinding(DataId id);
const AssetSpec *findAsset(const Package &package, const char *name);

}  // namespace BmsUi
