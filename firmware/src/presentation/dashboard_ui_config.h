#pragma once

#include <stdint.h>

namespace DashboardUiConfig {

constexpr int16_t GlobalOffsetX = 0;
constexpr int16_t GlobalOffsetY = 0;

namespace Palette {
constexpr uint32_t Yellow = 0xFFF200;
constexpr uint32_t White = 0xFFFFFF;
constexpr uint32_t LightGray = 0xD8D8D8;
constexpr uint32_t Cyan = 0x00E8F2;
constexpr uint32_t Green = 0x7CFF00;
constexpr uint32_t Orange = 0xFF9D00;
constexpr uint32_t OrangeRed = 0xFF5A1F;
constexpr uint32_t Red = 0xFF3B30;
}

namespace Theme {
constexpr uint32_t BackgroundColor = 0x000000;
constexpr uint32_t FrameColor = 0xFFF200;
constexpr uint32_t SocRingColor = 0xFFF200;
constexpr uint32_t LowSocRingColor = 0xFF5A1F;
constexpr uint32_t BluetoothColor = 0xFFFFFF;
}

enum FontId : uint8_t {
  FontMontserrat14,
  FontMontserrat16,
  FontMontserrat22,
  FontMontserrat32,
  FontMontserrat48,
  FontChinese16,
};

enum AlignId : uint8_t {
  AlignLeft,
  AlignCenter,
  AlignRight,
};

struct Rect {
  int16_t x;
  int16_t y;
  int16_t width;
  int16_t height;
};

struct LabelStyle {
  Rect rect;
  uint32_t color;
  FontId font;
  uint16_t zoom;
  AlignId align;
  int8_t letterSpacing;
  uint8_t boldLevel;
};

namespace Frame {
constexpr int16_t X = 0;
constexpr int16_t Y = 0;
}

namespace SocRing {
constexpr int16_t X = 41;
constexpr int16_t Y = 36;
constexpr int16_t Size = 122;
constexpr int16_t Width = 17;

constexpr uint8_t CellCount = 60;
constexpr uint8_t GapPixels = 2;
constexpr int16_t CellPitchDegrees = 6;

constexpr int16_t GapDegrees = 2;
constexpr int16_t CellSweepDegrees =
    CellPitchDegrees - GapDegrees;
constexpr int16_t StartAngle = 271;
constexpr uint8_t LowSocThresholdPercent = 25;

static_assert(CellCount == 60U, "SOC ring must contain exactly 60 cells");
static_assert(CellSweepDegrees > 0,
              "SOC cell sweep must be greater than zero");
}

namespace Bluetooth {
constexpr int16_t CenterX = 294;
constexpr int16_t CenterY = 17;
constexpr int16_t HalfHeight = 9;
constexpr int16_t HalfWidth = 7;
constexpr uint8_t LineWidth = 2;
}

namespace Formatting {
constexpr uint8_t RangeDecimals = 1;
constexpr uint8_t CapacityDecimals = 0;
constexpr uint8_t VoltageDecimals = 1;
constexpr uint8_t CurrentDecimals = 1;
constexpr uint8_t PowerDecimals = 1;
constexpr uint8_t DeltaVoltageDecimals = 3;
constexpr bool CapacitySpacesAroundSlash = true;
}

namespace Elements {

namespace Bms {
constexpr char Text[] = "BMS";

constexpr LabelStyle Label = {
    {10, 10, 42, 18},
    0xFFF200,
    FontMontserrat16,
    256,
    AlignLeft,
    0,
    50};
}

namespace Soc {
constexpr char TitleText[] = "SOC";
constexpr char UnitText[] = "%";

constexpr LabelStyle Title = {
    {80, 59, 46, 20},
    0xD8D8D8,
    FontMontserrat16,
    256,
    AlignCenter,
    0,
    20};

constexpr LabelStyle Value = {
    {55, 75, 92, 57},
    0x7CFF00,
    FontMontserrat48,
    256,
    AlignCenter,
    1,
    3};

constexpr LabelStyle Unit = {
    {123, 105, 24, 27},
    0x7CFF00,
    FontMontserrat14,
    256,
    AlignCenter,
    0,
    1};
}

namespace Charging {
constexpr float CurrentThresholdA = 0.50f;
constexpr float CurrentExitThresholdA = 0.15f;
constexpr uint32_t BlinkIntervalMs = 500;

constexpr LabelStyle Icon = {
    {79, 47, 48, 32},
    0xFF3B30,
    FontMontserrat16,
    256,
    AlignCenter,
    0,
    0};
constexpr uint8_t LineWidth = 2;
}

namespace Range {
constexpr char TitleText[] = "剩余里程";
constexpr char UnitText[] = "km";

constexpr LabelStyle Title = {
    {181, 42, 72, 20},
    0xFFFFFF,
    FontChinese16,
    256,
    AlignLeft,
    0,
    1};

constexpr LabelStyle Value = {
    {181, 65, 108, 43},
    0xFFF200,
    FontMontserrat32,
    256,
    AlignLeft,
    1,
    2};

constexpr LabelStyle Unit = {
    {280, 81, 20, 18},
    0xFFF200,
    FontMontserrat14,
    256,
    AlignLeft,
    0,
    3};
}

namespace Capacity {
constexpr char TitleText[] = "剩余容量";
constexpr char UnitText[] = "Ah";

constexpr LabelStyle Title = {
    {181, 116, 72, 20},
    0xFFFFFF,
    FontChinese16,
    256,
    AlignLeft,
    0,
    1};

constexpr LabelStyle Value = {
    {181, 136, 96, 31},
    0x7CFF00,
    FontMontserrat22,
    256,
    AlignLeft,
    1,
    2};

constexpr LabelStyle Unit = {
    {280, 143, 25, 19},
    0x7CFF00,
    FontMontserrat14,
    256,
    AlignLeft,
    0,
    3};
}

namespace Voltage {
constexpr char TitleText[] = "电压";
constexpr char UnitText[] = "V";

constexpr LabelStyle Title = {
    {5, 181, 48, 18},
    0x00E8F2,
    FontChinese16,
    256,
    AlignCenter,
    0,
    1};

constexpr LabelStyle Value = {
    {5, 200, 48, 18},
    0xFFFFFF,
    FontMontserrat14,
    256,
    AlignCenter,
    1,
    1};

constexpr LabelStyle Unit = {
    {40, 215, 10, 15},
    0xFFF200,
    FontMontserrat14,
    256,
    AlignRight,
    0,
    3};
}

namespace Current {
constexpr char TitleText[] = "电流";
constexpr char UnitText[] = "A";

constexpr LabelStyle Title = {
    {57, 181, 48, 18},
    0x00E8F2,
    FontChinese16,
    256,
    AlignCenter,
    0,
    1};

constexpr LabelStyle Value = {
    {57, 200, 48, 18},
    0xFFFFFF,
    FontMontserrat14,
    256,
    AlignCenter,
    1,
    1};

constexpr LabelStyle Unit = {
    {92, 215, 10, 15},
    0xFFF200,
    FontMontserrat14,
    256,
    AlignRight,
    0,
    3};
}

namespace Power {
constexpr char TitleText[] = "功率";
constexpr char UnitText[] = "W";

constexpr LabelStyle Title = {
    {109, 181, 48, 18},
    0x00E8F2,
    FontChinese16,
    256,
    AlignCenter,
    0,
    1};

constexpr LabelStyle Value = {
    {109, 200, 48, 18},
    0xFFFFFF,
    FontMontserrat14,
    256,
    AlignCenter,
    1,
    1};

constexpr LabelStyle Unit = {
    {144, 215, 10, 15},
    0xFFF200,
    FontMontserrat14,
    256,
    AlignRight,
    0,
    3};
}

namespace Temperature {
constexpr char TitleText[] = "温度";
constexpr char UnitText[] = "°C";

constexpr LabelStyle Title = {
    {161, 181, 48, 18},
    0x00E8F2,
    FontChinese16,
    256,
    AlignCenter,
    0,
    1};

constexpr LabelStyle Value = {
    {161, 200, 48, 18},
    0xFFFFFF,
    FontMontserrat14,
    256,
    AlignCenter,
    1,
    1};

constexpr LabelStyle Unit = {
    {187, 215, 19, 15},
    0xFFF200,
    FontMontserrat14,
    256,
    AlignRight,
    0,
    3};
}

namespace DeltaVoltage {
constexpr char TitleText[] = "压差";
constexpr char UnitText[] = "V";

constexpr LabelStyle Title = {
    {213, 181, 48, 18},
    0x00E8F2,
    FontChinese16,
    256,
    AlignCenter,
    0,
    1};

constexpr LabelStyle Value = {
    {213, 200, 48, 18},
    0xFFFFFF,
    FontMontserrat14,
    256,
    AlignCenter,
    1,
    1};

constexpr LabelStyle Unit = {
    {248, 215, 10, 15},
    0xFFF200,
    FontMontserrat14,
    256,
    AlignRight,
    0,
    3};
}

namespace Cycle {
constexpr char TitleText[] = "循环";
constexpr char UnitText[] = "C";

constexpr LabelStyle Title = {
    {265, 181, 48, 18},
    0x00E8F2,
    FontChinese16,
    256,
    AlignCenter,
    0,
    1};

constexpr LabelStyle Value = {
    {265, 200, 48, 18},
    0xFFFFFF,
    FontMontserrat14,
    256,
    AlignCenter,
    1,
    1};

constexpr LabelStyle Unit = {
    {300, 215, 10, 15},
    0xFFF200,
    FontMontserrat14,
    256,
    AlignRight,
    0,
    3};
}

}

}
