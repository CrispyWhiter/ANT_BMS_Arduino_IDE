#include <assert.h>
#include <string>

#include "../src/ui_runtime/bmsui_parser.h"

uint32_t millis() { return 0; }

class MemoryStream : public Stream {
 public:
  explicit MemoryStream(std::string data) : data_(std::move(data)) {}
  int available() override { return position_ < data_.size() ? static_cast<int>(data_.size() - position_) : 0; }
  int read() override { return position_ < data_.size() ? static_cast<unsigned char>(data_[position_++]) : -1; }
  int peek() override { return position_ < data_.size() ? static_cast<unsigned char>(data_[position_]) : -1; }
 private:
  std::string data_;
  size_t position_ = 0;
};

int main() {
  const std::string valid =
      "BMSUI|1|320|240|000000|Demo%20UI\n"
      "LABEL|soc|20|20|100|40|M32|7CFF00|C|SOC|0||%25|\n"
      "ARC|ring|10|10|120|120|0|100|SOC|7CFF00|8|135|405\n"
      "BAR|bar|20|200|280|14|0|100|SOC|7CFF00|203026|7\n";
  MemoryStream stream(valid);
  BmsUi::Package package;
  BmsUi::ParseResult result;
  assert(BmsUi::parse(stream, package, result));
  assert(result.ok);
  assert(package.widgetCount == 3);
  assert(std::string(package.title) == "Demo UI");
  assert(package.widgets[0].binding == BmsUi::DataId::Soc);
  assert(std::string(package.widgets[0].suffix) == "%");


  const std::string staticText =
      "BMSUI|1|320|240|000000|Text%20Test\n"
      "LABEL|title|8|8|300|40|M16|FFFFFF|L|NONE|0|||Battery%20Status%20Ready\n";
  MemoryStream staticTextStream(staticText);
  BmsUi::Package staticTextPackage;
  BmsUi::ParseResult staticTextResult;
  assert(BmsUi::parse(staticTextStream, staticTextPackage, staticTextResult));
  assert(std::string(staticTextPackage.widgets[0].text) == "Battery Status Ready");

  const std::string invalid =
      "BMSUI|1|320|240|000000|Bad\n"
      "LABEL|x|0|0|100|20|M16|FFFFFF|C|NO_SUCH_BINDING|0|||x\n";
  MemoryStream bad(invalid);
  BmsUi::Package badPackage;
  BmsUi::ParseResult badResult;
  assert(!BmsUi::parse(bad, badPackage, badResult));
  assert(!badResult.ok);
  assert(badResult.line == 2);

  const std::string badPercent =
      "BMSUI|1|320|240|000000|Bad\n"
      "LABEL|x|0|0|100|20|M16|FFFFFF|C|NONE|0|||bad%\n";
  MemoryStream badPercentStream(badPercent);
  BmsUi::Package badPercentPackage;
  BmsUi::ParseResult badPercentResult;
  assert(!BmsUi::parse(badPercentStream, badPercentPackage, badPercentResult));
  assert(badPercentResult.line == 2);

  const std::string zeroIconHex(192U * 2U, '0');
  const std::string stateIcons =
      "BMSUI|2|320|240|000000|State%20Icons\n"
      "STATEIMG|ble_icon|10|10|8|8|CONNECTED|ble_on|ble_off\n"
      "ASSET|ble_on|8|8|RGB565A8|192|8BFF08F2\n"
      "ASSETDATA|ble_on|" + zeroIconHex + "\n" +
      "ASSET|ble_off|8|8|RGB565A8|192|8BFF08F2\n"
      "ASSETDATA|ble_off|" + zeroIconHex + "\n";
  MemoryStream stateIconStream(stateIcons);
  BmsUi::Package stateIconPackage;
  BmsUi::ParseResult stateIconResult;
  assert(BmsUi::parse(stateIconStream, stateIconPackage, stateIconResult));
  assert(stateIconPackage.version == 2);
  assert(stateIconPackage.widgetCount == 1);
  assert(stateIconPackage.assetCount == 2);
  assert(stateIconPackage.widgets[0].type == BmsUi::WidgetType::StateImage);
  assert(stateIconPackage.widgets[0].binding == BmsUi::DataId::Connected);
  assert(std::string(stateIconPackage.widgets[0].onAsset) == "ble_on");
  assert(std::string(stateIconPackage.widgets[0].offAsset) == "ble_off");

  const std::string nonSocBar =
      "BMSUI|2|320|240|000000|Bad%20Bar\n"
      "BAR|voltage_bar|20|200|280|14|0|100|VOLTAGE|7CFF00|203026|7\n";
  MemoryStream nonSocBarStream(nonSocBar);
  BmsUi::Package nonSocBarPackage;
  BmsUi::ParseResult nonSocBarResult;
  assert(!BmsUi::parse(nonSocBarStream, nonSocBarPackage, nonSocBarResult));
  assert(nonSocBarResult.line == 2);


  const std::string capacityAndRangeBars =
      "BMSUI|3|320|240|000000|Progress%20Bars\n"
      "BAR|capacity_bar|10|20|200|14|0|100|REMAINING_AH|4CC9FF|203026|7|ROUND|100\n"
      "BAR|range_bar|10|50|200|14|0|100|RANGE_KM|38E0A0|203026|7|ROUND|100\n"
      "ARC|soc_arc_color|220|20|80|80|0|100|SOC|7CFF00|203026|7|135|405|ROUND|100\n";
  MemoryStream capacityRangeStream(capacityAndRangeBars);
  BmsUi::Package capacityRangePackage;
  BmsUi::ParseResult capacityRangeResult;
  assert(BmsUi::parse(capacityRangeStream, capacityRangePackage, capacityRangeResult));
  assert(capacityRangePackage.widgetCount == 3);
  assert(capacityRangePackage.widgets[0].binding == BmsUi::DataId::RemainingCapacityAh);
  assert(capacityRangePackage.widgets[1].binding == BmsUi::DataId::RemainingRangeKm);
  assert(capacityRangePackage.widgets[2].secondaryColor == 0x203026U);

  const std::string legacyVoltageBar =
      "BMSUI|1|320|240|000000|Legacy\n"
      "BAR|voltage_bar|20|200|280|14|0|100|VOLTAGE|7CFF00|203026|7\n";
  MemoryStream legacyVoltageBarStream(legacyVoltageBar);
  BmsUi::Package legacyVoltageBarPackage;
  BmsUi::ParseResult legacyVoltageBarResult;
  assert(BmsUi::parse(legacyVoltageBarStream, legacyVoltageBarPackage, legacyVoltageBarResult));
  assert(legacyVoltageBarPackage.widgets[0].binding == BmsUi::DataId::TotalVoltage);

  const std::string zeroTinyHex(12U * 2U, '0');
  const std::string v3Theme =
      "BMSUI|3|320|240|000000|V3%20Theme\n"
      "LABEL|voltage|10|10|100|24|M16|FFFFFF|L|VOLTAGE|1||%20V||72\n"
      "BAR|soc_bar|10|40|180|12|0|100|SOC|7CFF00|203026|0|SQUARE|65\n"
      "ARC|soc_arc|200|20|80|80|0|100|SOC|7CFF00|7|135|405|ROUND|80\n"
      "IMAGE|logo|10|70|2|2|logo_img|55\n"
      "TEXTIMG|title|20|80|2|2|title_text|90|Microsoft%20YaHei%20UI|16|1|L|FFFFFF|%E7%94%B5%E6%B1%A0\n"
      "SHAPE|shape1|30|100|2|2|shape_asset|70|ELLIPSE|ROUND|1|FF0000|FFFFFF|1|3|0\n"
      "ASSET|logo_img|2|2|RGB565A8|12|7BD5C66F\n"
      "ASSETDATA|logo_img|" + zeroTinyHex + "\n" +
      "ASSET|title_text|2|2|A8|4|2144DF1C\n"
      "ASSETDATA|title_text|00000000\n" +
      "ASSET|shape_asset|2|2|RGB565A8|12|7BD5C66F\n"
      "ASSETDATA|shape_asset|" + zeroTinyHex + "\n";
  MemoryStream v3Stream(v3Theme);
  BmsUi::Package v3Package;
  BmsUi::ParseResult v3Result;
  assert(BmsUi::parse(v3Stream, v3Package, v3Result));
  assert(v3Package.version == 3);
  assert(v3Package.widgetCount == 6);
  assert(v3Package.assetCount == 3);
  assert(v3Package.assets[1].format == BmsUi::AssetFormat::Alpha8);
  assert(v3Package.widgets[0].opacity == 72);
  assert(v3Package.widgets[1].type == BmsUi::WidgetType::Bar);
  assert(!v3Package.widgets[1].rounded);
  assert(v3Package.widgets[1].opacity == 65);
  assert(v3Package.widgets[2].type == BmsUi::WidgetType::Arc);
  assert(v3Package.widgets[2].rounded);
  assert(v3Package.widgets[3].type == BmsUi::WidgetType::Image);
  assert(std::string(v3Package.widgets[3].asset) == "logo_img");
  assert(v3Package.widgets[4].type == BmsUi::WidgetType::TextImage);
  assert(v3Package.widgets[5].type == BmsUi::WidgetType::Shape);


  const std::string v4Gradient =
      "BMSUI|4|320|240|000000|Gradient\n"
      "BAR|soc_grad|10|20|200|16|0|100|SOC|7CFF00|203026|8|ROUND|100|1|00D9FF|HORIZONTAL\n"
      "ARC|soc_grad_ring|220|20|80|80|0|100|SOC|7CFF00|203026|7|135|405|ROUND|100|1|00D9FF\n";
  MemoryStream v4Stream(v4Gradient);
  BmsUi::Package v4Package;
  BmsUi::ParseResult v4Result;
  assert(BmsUi::parse(v4Stream, v4Package, v4Result));
  assert(v4Package.version == 4);
  assert(v4Package.widgetCount == 2);
  assert(v4Package.widgets[0].gradientEnabled);
  assert(v4Package.widgets[0].gradientColor == 0x00D9FFU);
  assert(v4Package.widgets[0].gradientDirection == BmsUi::GradientDirection::Horizontal);
  assert(v4Package.widgets[1].gradientEnabled);
  assert(v4Package.widgets[1].gradientColor == 0x00D9FFU);

  const std::string v5Vector =
      "BMSUI|5|320|240|000000|Vector\n"
      "VSHAPE|card|20|20|160|44|RECT|ROUND|0|000000|5A5A5A|1|8|100\n"
      "VSHAPE|dot|210|24|24|24|CIRCLE|ROUND|1|00D9FF|00D9FF|0|12|85\n";
  MemoryStream v5Stream(v5Vector);
  BmsUi::Package v5Package;
  BmsUi::ParseResult v5Result;
  assert(BmsUi::parse(v5Stream, v5Package, v5Result));
  assert(v5Package.version == 5);
  assert(v5Package.widgetCount == 2);
  assert(v5Package.assetCount == 0);
  assert(v5Package.widgets[0].type == BmsUi::WidgetType::VectorShape);
  assert(v5Package.widgets[0].shapeKind == BmsUi::ShapeKind::Rect);
  assert(v5Package.widgets[0].cornerKind == BmsUi::CornerKind::Round);
  assert(!v5Package.widgets[0].fill);
  assert(v5Package.widgets[0].borderWidth == 1);
  assert(v5Package.widgets[1].shapeKind == BmsUi::ShapeKind::Circle);
  assert(v5Package.widgets[1].fill);
  assert(v5Package.widgets[1].opacity == 85);

  const std::string zeroGlyphHex(256U * 2U, '0');
  const std::string v6Glyph =
      "BMSUI|6|320|240|000000|Font%20Fidelity\n"
      "GLYPHVAL|soc_value|20|20|100|16|gfont1|100|FFFFFF|R|SOC|0|||Segoe%20UI|16|1|8|2|30,31|8,8\n"
      "ASSET|gfont1|16|16|A8|256|0D968558\n"
      "ASSETDATA|gfont1|" + zeroGlyphHex + "\n";
  MemoryStream v6Stream(v6Glyph);
  BmsUi::Package v6Package;
  BmsUi::ParseResult v6Result;
  assert(BmsUi::parse(v6Stream, v6Package, v6Result));
  assert(v6Package.version == 6);
  assert(v6Package.widgetCount == 1);
  assert(v6Package.assetCount == 1);
  assert(v6Package.widgets[0].type == BmsUi::WidgetType::GlyphValue);
  assert(v6Package.widgets[0].glyphCount == 2);
  assert(v6Package.widgets[0].glyphCodepoints[0] == 0x30U);
  assert(v6Package.widgets[0].glyphAdvances[1] == 8U);

  const std::string v7ImageOpt =
      "BMSUI|7|320|240|000000|Image%20Opt\n"
      "MASKIMG|mask|10|10|2|2|mask_a8|100|FFFFFF\n"
      "STATEMASK|state|20|10|2|2|CONNECTED|state_on|state_off|00FF00|606060|90\n"
      "IMAGE|opaque|30|10|2|2|opaque565|100\n"
      "ASSET|mask_a8|2|2|A8|4|2144DF1C\n"
      "ASSETB64|mask_a8|AAAAAA==\n"
      "ASSET|state_on|2|2|A8|4|2144DF1C\n"
      "ASSETB64|state_on|AAAAAA==\n"
      "ASSET|state_off|2|2|A8|4|2144DF1C\n"
      "ASSETB64|state_off|AAAAAA==\n"
      "ASSET|opaque565|2|2|RGB565|8|6522DF69\n"
      "ASSETB64|opaque565|AAAAAAAAAAA=\n";
  MemoryStream v7Stream(v7ImageOpt);
  BmsUi::Package v7Package;
  BmsUi::ParseResult v7Result;
  assert(BmsUi::parse(v7Stream, v7Package, v7Result));
  assert(v7Package.version == 7);
  assert(v7Package.widgetCount == 3);
  assert(v7Package.assets[0].format == BmsUi::AssetFormat::Alpha8);
  assert(v7Package.widgets[0].type == BmsUi::WidgetType::TextImage);
  assert(v7Package.widgets[0].color == 0xFFFFFFU);
  assert(v7Package.widgets[1].type == BmsUi::WidgetType::StateImage);
  assert(v7Package.widgets[1].color == 0x00FF00U);
  assert(v7Package.widgets[1].secondaryColor == 0x606060U);
  assert(v7Package.assets[3].format == BmsUi::AssetFormat::RGB565);

  return 0;
}
