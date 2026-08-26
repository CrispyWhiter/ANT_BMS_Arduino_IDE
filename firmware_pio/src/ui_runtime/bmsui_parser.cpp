#include "bmsui_parser.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

namespace BmsUi {
namespace {

constexpr size_t kMaxFields = 24;

void setError(ParseResult &result, uint16_t line, const char *message) {
  result.ok = false;
  result.line = line;
  snprintf(result.message, sizeof(result.message), "%s", message == nullptr ? "parse error" : message);
}

void trimRight(char *text) {
  if (text == nullptr) return;
  size_t length = strlen(text);
  while (length > 0U && (text[length - 1U] == '\r' || text[length - 1U] == '\n' ||
                         text[length - 1U] == ' ' || text[length - 1U] == '\t')) {
    text[--length] = '\0';
  }
}

char *trimLeft(char *text) {
  if (text == nullptr) return text;
  while (*text == ' ' || *text == '\t') ++text;
  return text;
}

size_t splitFields(char *line, char **fields, size_t capacity) {
  if (line == nullptr || fields == nullptr || capacity == 0U) return 0U;
  size_t count = 0U;
  char *cursor = line;
  fields[count++] = cursor;
  while (*cursor != '\0') {
    if (*cursor == '|') {
      *cursor = '\0';
      if (count >= capacity) return capacity + 1U;
      fields[count++] = cursor + 1;
    }
    ++cursor;
  }
  return count;
}

bool parseLong(const char *text, long minimum, long maximum, long &value) {
  if (text == nullptr || *text == '\0') return false;
  char *end = nullptr;
  const long parsed = strtol(text, &end, 10);
  if (end == text || *end != '\0' || parsed < minimum || parsed > maximum) return false;
  value = parsed;
  return true;
}

bool parseUnsignedHex(const char *text, uint32_t &value) {
  if (text == nullptr || *text == '\0') return false;
  char *end = nullptr;
  const unsigned long parsed = strtoul(text, &end, 16);
  if (end == text || *end != '\0') return false;
  value = static_cast<uint32_t>(parsed);
  return true;
}

bool parseFloatValue(const char *text, float minimum, float maximum, float &value) {
  if (text == nullptr || *text == '\0') return false;
  char *end = nullptr;
  const float parsed = strtof(text, &end);
  if (end == text || *end != '\0' || !isfinite(parsed) || parsed < minimum || parsed > maximum) return false;
  value = parsed;
  return true;
}

bool parseHexColor(const char *text, uint32_t &color) {
  if (text == nullptr) return false;
  if (*text == '#') ++text;
  if (strlen(text) != 6U) return false;
  uint32_t value = 0U;
  for (size_t i = 0; i < 6U; ++i) {
    const char c = text[i];
    uint8_t nibble = 0U;
    if (c >= '0' && c <= '9') nibble = static_cast<uint8_t>(c - '0');
    else if (c >= 'a' && c <= 'f') nibble = static_cast<uint8_t>(10 + c - 'a');
    else if (c >= 'A' && c <= 'F') nibble = static_cast<uint8_t>(10 + c - 'A');
    else return false;
    value = (value << 4U) | nibble;
  }
  color = value;
  return true;
}

int hexValue(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return 10 + c - 'a';
  if (c >= 'A' && c <= 'F') return 10 + c - 'A';
  return -1;
}

bool decodeText(const char *encoded, char *output, size_t capacity) {
  if (encoded == nullptr || output == nullptr || capacity == 0U) return false;
  size_t out = 0U;
  for (size_t i = 0U; encoded[i] != '\0'; ++i) {
    uint8_t value = static_cast<uint8_t>(encoded[i]);
    if (encoded[i] == '%') {
      if (encoded[i + 1U] == '\0' || encoded[i + 2U] == '\0') return false;
      const int high = hexValue(encoded[i + 1U]);
      const int low = hexValue(encoded[i + 2U]);
      if (high < 0 || low < 0) return false;
      value = static_cast<uint8_t>((high << 4) | low);
      i += 2U;
    }
    if (out + 1U >= capacity) return false;
    output[out++] = static_cast<char>(value);
  }
  output[out] = '\0';
  return true;
}

bool copyToken(const char *source, char *destination, size_t capacity) {
  if (source == nullptr || source[0] == '\0' || strlen(source) >= capacity) return false;
  for (const char *cursor = source; *cursor != '\0'; ++cursor) {
    const bool allowed = isalnum(static_cast<unsigned char>(*cursor)) || *cursor == '_' || *cursor == '-';
    if (!allowed) return false;
  }
  snprintf(destination, capacity, "%s", source);
  return true;
}

bool uniqueId(const Package &package, const char *id) {
  for (uint16_t i = 0U; i < package.widgetCount; ++i) {
    if (strcmp(package.widgets[i].id, id) == 0) return false;
  }
  return true;
}

AssetSpec *findAssetMutable(Package &package, const char *name) {
  for (uint16_t i = 0U; i < package.assetCount; ++i) {
    if (strcmp(package.assets[i].name, name) == 0) return &package.assets[i];
  }
  return nullptr;
}

uint32_t crc32Update(uint32_t crc, uint8_t value) {
  crc ^= value;
  for (uint8_t bit = 0; bit < 8U; ++bit) crc = (crc >> 1U) ^ (0xEDB88320U & (0U - (crc & 1U)));
  return crc;
}

bool parseRectFields(char **fields, size_t count, WidgetSpec &spec) {
  if (count < 6U) return false;
  long value = 0;
  if (!parseLong(fields[2], -320, 640, value)) return false;
  spec.x = static_cast<int16_t>(value);
  if (!parseLong(fields[3], -240, 480, value)) return false;
  spec.y = static_cast<int16_t>(value);
  if (!parseLong(fields[4], 1, 640, value)) return false;
  spec.width = static_cast<int16_t>(value);
  if (!parseLong(fields[5], 1, 480, value)) return false;
  spec.height = static_cast<int16_t>(value);
  return true;
}

bool parseHeader(char **fields, size_t count, Package &package, ParseResult &result, uint16_t lineNumber) {
  if (count != 6U || strcasecmp(fields[0], "BMSUI") != 0) { setError(result, lineNumber, "first line must be BMSUI header"); return false; }
  long value = 0;
  if (!parseLong(fields[1], LegacyProtocolVersion, ProtocolVersion, value)) {
    setError(result, lineNumber, "unsupported BMSUI protocol version"); return false;
  }
  package.version = static_cast<uint16_t>(value);
  if (!parseLong(fields[2], 1, 4096, value) || value != AppConfig::Display::Width) { setError(result, lineNumber, "canvas width does not match firmware display"); return false; }
  package.width = static_cast<uint16_t>(value);
  if (!parseLong(fields[3], 1, 4096, value) || value != AppConfig::Display::Height) { setError(result, lineNumber, "canvas height does not match firmware display"); return false; }
  package.height = static_cast<uint16_t>(value);
  if (!parseHexColor(fields[4], package.backgroundColor)) { setError(result, lineNumber, "invalid header background color"); return false; }
  if (!decodeText(fields[5], package.title, sizeof(package.title))) { setError(result, lineNumber, "invalid or oversized title"); return false; }
  return true;
}

bool parseOpacity(const char *text, uint8_t &opacity) {
  long value = 0;
  if (!parseLong(text, 0, 100, value)) return false;
  opacity = static_cast<uint8_t>(value);
  return true;
}

bool parseCapStyle(const char *text, bool &rounded) {
  if (strcasecmp(text, "ROUND") == 0) { rounded = true; return true; }
  if (strcasecmp(text, "SQUARE") == 0 || strcasecmp(text, "STRAIGHT") == 0) { rounded = false; return true; }
  return false;
}

bool parseGradientDirection(const char *text, GradientDirection &direction) {
  if (strcasecmp(text, "AUTO") == 0) { direction = GradientDirection::Auto; return true; }
  if (strcasecmp(text, "H") == 0 || strcasecmp(text, "HORIZONTAL") == 0) { direction = GradientDirection::Horizontal; return true; }
  if (strcasecmp(text, "V") == 0 || strcasecmp(text, "VERTICAL") == 0) { direction = GradientDirection::Vertical; return true; }
  return false;
}

bool parseLabel(char **fields, size_t count, WidgetSpec &spec) {
  if ((count != 14U && count != 15U) || !parseRectFields(fields, count, spec)) return false;
  spec.type = WidgetType::Label;
  if (!parseFontId(fields[6], spec.font) || !parseHexColor(fields[7], spec.color) || !parseAlignId(fields[8], spec.align) || !parseDataId(fields[9], spec.binding)) return false;
  long decimals = 0;
  if (!parseLong(fields[10], 0, 3, decimals)) return false;
  spec.decimals = static_cast<uint8_t>(decimals);
  if (count == 15U && !parseOpacity(fields[14], spec.opacity)) return false;
  return decodeText(fields[11], spec.prefix, sizeof(spec.prefix)) && decodeText(fields[12], spec.suffix, sizeof(spec.suffix)) && decodeText(fields[13], spec.text, sizeof(spec.text));
}

bool parseRect(char **fields, size_t count, WidgetSpec &spec) {
  if ((count != 10U && count != 11U) || !parseRectFields(fields, count, spec)) return false;
  spec.type = WidgetType::Rect;
  if (!parseHexColor(fields[6], spec.color) || !parseHexColor(fields[7], spec.borderColor)) return false;
  long value = 0;
  if (!parseLong(fields[8], 0, 16, value)) return false;
  spec.borderWidth = static_cast<uint8_t>(value);
  if (!parseLong(fields[9], 0, 64, value)) return false;
  spec.radius = static_cast<uint8_t>(value);
  return count == 10U || parseOpacity(fields[10], spec.opacity);
}

bool parseLine(char **fields, size_t count, WidgetSpec &spec) {
  if (count != 9U && count != 10U) return false;
  spec.type = WidgetType::Line;
  long value = 0;
  if (!parseLong(fields[2], -320, 640, value)) return false;
  spec.x = static_cast<int16_t>(value);
  if (!parseLong(fields[3], -240, 480, value)) return false;
  spec.y = static_cast<int16_t>(value);
  if (!parseLong(fields[4], -320, 640, value)) return false;
  spec.x2 = static_cast<int16_t>(value);
  if (!parseLong(fields[5], -240, 480, value)) return false;
  spec.y2 = static_cast<int16_t>(value);
  if (!parseHexColor(fields[6], spec.color)) return false;
  if (!parseLong(fields[7], 1, 16, value)) return false;
  spec.lineWidth = static_cast<uint8_t>(value);
  if (strcasecmp(fields[8], "SOLID") != 0) return false;
  return count == 9U || parseOpacity(fields[9], spec.opacity);
}

bool parseArc(char **fields, size_t count, WidgetSpec &spec) {
  if ((count != 13U && count != 15U && count != 16U && count != 18U) || !parseRectFields(fields, count, spec)) return false;
  spec.type = WidgetType::Arc;
  if (!parseFloatValue(fields[6], -1000000.0f, 1000000.0f, spec.minValue)) return false;
  if (!parseFloatValue(fields[7], -1000000.0f, 1000000.0f, spec.maxValue) || spec.maxValue <= spec.minValue) return false;
  if (!parseDataId(fields[8], spec.binding) || spec.binding == DataId::None) return false;
  if (!parseHexColor(fields[9], spec.color)) return false;

  size_t widthIndex = 10U;
  size_t startIndex = 11U;
  size_t endIndex = 12U;
  if (count == 16U || count == 18U) {
    if (!parseHexColor(fields[10], spec.secondaryColor)) return false;
    widthIndex = 11U;
    startIndex = 12U;
    endIndex = 13U;
  }

  long value = 0;
  if (!parseLong(fields[widthIndex], 1, 32, value)) return false;
  spec.lineWidth = static_cast<uint8_t>(value);
  if (!parseLong(fields[startIndex], -720, 720, value)) return false;
  spec.startAngle = static_cast<int16_t>(value);
  if (!parseLong(fields[endIndex], -720, 720, value)) return false;
  spec.endAngle = static_cast<int16_t>(value);
  if (spec.endAngle <= spec.startAngle || spec.endAngle - spec.startAngle >= 360) return false;

  if (count == 15U && (!parseCapStyle(fields[13], spec.rounded) || !parseOpacity(fields[14], spec.opacity))) return false;
  if (count == 16U && (!parseCapStyle(fields[14], spec.rounded) || !parseOpacity(fields[15], spec.opacity))) return false;
  if (count == 18U) {
    if (!parseCapStyle(fields[14], spec.rounded) || !parseOpacity(fields[15], spec.opacity)) return false;
    long enabled = 0;
    if (!parseLong(fields[16], 0, 1, enabled) || !parseHexColor(fields[17], spec.gradientColor)) return false;
    spec.gradientEnabled = enabled != 0;
  }
  return true;
}

bool parseBar(char **fields, size_t count, WidgetSpec &spec) {
  if ((count != 12U && count != 14U && count != 17U) || !parseRectFields(fields, count, spec)) return false;
  spec.type = WidgetType::Bar;
  if (!parseFloatValue(fields[6], -1000000.0f, 1000000.0f, spec.minValue)) return false;
  if (!parseFloatValue(fields[7], -1000000.0f, 1000000.0f, spec.maxValue) || spec.maxValue <= spec.minValue) return false;
  if (!parseDataId(fields[8], spec.binding) || spec.binding == DataId::None) return false;
  if (!parseHexColor(fields[9], spec.color) || !parseHexColor(fields[10], spec.secondaryColor)) return false;
  long value = 0;
  if (!parseLong(fields[11], 0, 64, value)) return false;
  spec.radius = static_cast<uint8_t>(value);
  if (count == 14U && (!parseCapStyle(fields[12], spec.rounded) || !parseOpacity(fields[13], spec.opacity))) return false;
  if (count == 17U) {
    if (!parseCapStyle(fields[12], spec.rounded) || !parseOpacity(fields[13], spec.opacity)) return false;
    long enabled = 0;
    if (!parseLong(fields[14], 0, 1, enabled) || !parseHexColor(fields[15], spec.gradientColor) || !parseGradientDirection(fields[16], spec.gradientDirection)) return false;
    spec.gradientEnabled = enabled != 0;
  }
  return true;
}

bool parseStateImage(char **fields, size_t count, WidgetSpec &spec) {
  if ((count != 9U && count != 10U) || !parseRectFields(fields, count, spec)) return false;
  spec.type = WidgetType::StateImage;
  if (!parseDataId(fields[6], spec.binding) || !isSwitchBinding(spec.binding)) return false;
  if (!copyToken(fields[7], spec.onAsset, sizeof(spec.onAsset)) || !copyToken(fields[8], spec.offAsset, sizeof(spec.offAsset))) return false;
  return count == 9U || parseOpacity(fields[9], spec.opacity);
}

bool parseImageLike(char **fields, size_t count, WidgetSpec &spec, WidgetType type) {
  if (count < 8U || !parseRectFields(fields, count, spec)) return false;
  spec.type = type;
  if (!copyToken(fields[6], spec.asset, sizeof(spec.asset)) || !parseOpacity(fields[7], spec.opacity)) return false;
  return true;
}

bool parseImage(char **fields, size_t count, WidgetSpec &spec) {
  return count == 8U && parseImageLike(fields, count, spec, WidgetType::Image);
}

bool parseMaskImage(char **fields, size_t count, WidgetSpec &spec) {
  if (count != 9U || !parseImageLike(fields, count, spec, WidgetType::TextImage)) return false;
  return parseHexColor(fields[8], spec.color);
}

bool parseStateMask(char **fields, size_t count, WidgetSpec &spec) {
  if (count != 12U || !parseRectFields(fields, count, spec)) return false;
  spec.type = WidgetType::StateImage;
  if (!parseDataId(fields[6], spec.binding) || !isSwitchBinding(spec.binding)) return false;
  if (!copyToken(fields[7], spec.onAsset, sizeof(spec.onAsset)) || !copyToken(fields[8], spec.offAsset, sizeof(spec.offAsset))) return false;
  if (!parseHexColor(fields[9], spec.color) || !parseHexColor(fields[10], spec.secondaryColor)) return false;
  return parseOpacity(fields[11], spec.opacity);
}

bool parseTextImage(char **fields, size_t count, WidgetSpec &spec) {
  if (count != 14U || !parseImageLike(fields, count, spec, WidgetType::TextImage)) return false;
  if (!parseHexColor(fields[12], spec.color)) return false;
  return true;
}

bool parseShape(char **fields, size_t count, WidgetSpec &spec) {
  return count == 16U && parseImageLike(fields, count, spec, WidgetType::Shape);
}

bool parseVectorShape(char **fields, size_t count, WidgetSpec &spec) {
  if (count != 14U || !parseRectFields(fields, count, spec)) return false;
  spec.type = WidgetType::VectorShape;
  if (strcasecmp(fields[6], "RECT") == 0) {
    spec.shapeKind = ShapeKind::Rect;
  } else if (strcasecmp(fields[6], "CIRCLE") == 0) {
    spec.shapeKind = ShapeKind::Circle;
    if (spec.width != spec.height) return false;
  } else {
    return false;
  }
  if (strcasecmp(fields[7], "SQUARE") == 0) {
    spec.cornerKind = CornerKind::Square;
  } else if (strcasecmp(fields[7], "ROUND") == 0) {
    spec.cornerKind = CornerKind::Round;
  } else {
    return false;
  }
  long value = 0;
  if (!parseLong(fields[8], 0, 1, value)) return false;
  spec.fill = value != 0;
  if (!parseHexColor(fields[9], spec.color) || !parseHexColor(fields[10], spec.borderColor)) return false;
  if (!parseLong(fields[11], 0, 16, value)) return false;
  spec.borderWidth = static_cast<uint8_t>(value);
  if (!parseLong(fields[12], 0, 120, value)) return false;
  spec.radius = static_cast<uint8_t>(value);
  if (!parseOpacity(fields[13], spec.opacity)) return false;
  return true;
}


bool parseGlyphHexList(const char *text, uint32_t *output, uint8_t &count) {
  if (text == nullptr || output == nullptr || *text == '\0') return false;
  count = 0U;
  const char *cursor = text;
  while (*cursor != '\0') {
    if (count >= MaxGlyphs) return false;
    char token[9] = {};
    size_t n = 0U;
    while (*cursor != '\0' && *cursor != ',') {
      if (n + 1U >= sizeof(token)) return false;
      token[n++] = *cursor++;
    }
    token[n] = '\0';
    uint32_t value = 0U;
    if (!parseUnsignedHex(token, value) || value == 0U || value > 0x10FFFFU) return false;
    output[count++] = value;
    if (*cursor == ',') ++cursor;
  }
  return count > 0U;
}

bool parseGlyphAdvanceList(const char *text, uint8_t *output, uint8_t expectedCount) {
  if (text == nullptr || output == nullptr || expectedCount == 0U) return false;
  uint8_t count = 0U;
  const char *cursor = text;
  while (*cursor != '\0') {
    if (count >= expectedCount) return false;
    char token[5] = {};
    size_t n = 0U;
    while (*cursor != '\0' && *cursor != ',') {
      if (n + 1U >= sizeof(token)) return false;
      token[n++] = *cursor++;
    }
    token[n] = '\0';
    long value = 0;
    if (!parseLong(token, 1, 120, value)) return false;
    output[count++] = static_cast<uint8_t>(value);
    if (*cursor == ',') ++cursor;
  }
  return count == expectedCount;
}

bool parseGlyphValue(char **fields, size_t count, WidgetSpec &spec) {
  if (count != 21U || !parseRectFields(fields, count, spec)) return false;
  spec.type = WidgetType::GlyphValue;
  if (!copyToken(fields[6], spec.asset, sizeof(spec.asset))) return false;
  if (!parseOpacity(fields[7], spec.opacity) || !parseHexColor(fields[8], spec.color)) return false;
  if (!parseAlignId(fields[9], spec.align) || !parseDataId(fields[10], spec.binding) || spec.binding == DataId::None) return false;
  long value = 0;
  if (!parseLong(fields[11], 0, 3, value)) return false;
  spec.decimals = static_cast<uint8_t>(value);
  if (!decodeText(fields[12], spec.prefix, sizeof(spec.prefix)) || !decodeText(fields[13], spec.suffix, sizeof(spec.suffix))) return false;
  char ignoredFont[96] = {};
  if (!decodeText(fields[14], ignoredFont, sizeof(ignoredFont))) return false;
  if (!parseLong(fields[15], 5, 96, value)) return false;
  spec.glyphFontSize = static_cast<uint8_t>(value);
  if (!parseLong(fields[16], 0, 1, value)) return false;
  spec.glyphBold = value != 0;
  if (!parseLong(fields[17], 1, 120, value)) return false;
  spec.glyphCellWidth = static_cast<uint8_t>(value);
  if (!parseLong(fields[18], 1, MaxGlyphs, value)) return false;
  spec.glyphColumns = static_cast<uint8_t>(value);
  if (!parseGlyphHexList(fields[19], spec.glyphCodepoints, spec.glyphCount)) return false;
  if (spec.glyphColumns > spec.glyphCount) spec.glyphColumns = spec.glyphCount;
  if (!parseGlyphAdvanceList(fields[20], spec.glyphAdvances, spec.glyphCount)) return false;
  return true;
}

bool parseWidget(char **fields, size_t count, Package &package, ParseResult &result, uint16_t lineNumber) {
  if (package.widgetCount >= MaxWidgets) { setError(result, lineNumber, "widget count exceeds firmware limit"); return false; }
  if (count < 2U) { setError(result, lineNumber, "widget line has too few fields"); return false; }
  WidgetSpec spec;
  if (!copyToken(fields[1], spec.id, sizeof(spec.id)) || !uniqueId(package, spec.id)) { setError(result, lineNumber, "invalid or duplicate widget id"); return false; }
  bool ok = false;
  if (strcasecmp(fields[0], "LABEL") == 0) ok = parseLabel(fields, count, spec);
  else if (strcasecmp(fields[0], "RECT") == 0) ok = parseRect(fields, count, spec);
  else if (strcasecmp(fields[0], "LINE") == 0) ok = parseLine(fields, count, spec);
  else if (strcasecmp(fields[0], "ARC") == 0) ok = parseArc(fields, count, spec);
  else if (strcasecmp(fields[0], "BAR") == 0) ok = parseBar(fields, count, spec);
  else if (strcasecmp(fields[0], "STATEIMG") == 0 && package.version >= 2U) ok = parseStateImage(fields, count, spec);
  else if (strcasecmp(fields[0], "STATEMASK") == 0 && package.version >= 7U) ok = parseStateMask(fields, count, spec);
  else if (strcasecmp(fields[0], "IMAGE") == 0 && package.version >= 3U) ok = parseImage(fields, count, spec);
  else if (strcasecmp(fields[0], "MASKIMG") == 0 && package.version >= 7U) ok = parseMaskImage(fields, count, spec);
  else if (strcasecmp(fields[0], "TEXTIMG") == 0 && package.version >= 3U) ok = parseTextImage(fields, count, spec);
  else if (strcasecmp(fields[0], "SHAPE") == 0 && package.version >= 3U) ok = parseShape(fields, count, spec);
  else if (strcasecmp(fields[0], "VSHAPE") == 0 && package.version >= 5U) ok = parseVectorShape(fields, count, spec);
  else if (strcasecmp(fields[0], "GLYPHVAL") == 0 && package.version >= 6U) ok = parseGlyphValue(fields, count, spec);
  else { setError(result, lineNumber, "unknown widget type"); return false; }
  if (ok && package.version >= 2U && spec.type == WidgetType::Arc && spec.binding != DataId::Soc) {
    ok = false;
  }
  if (ok && package.version >= 2U && spec.type == WidgetType::Bar &&
      spec.binding != DataId::Soc &&
      spec.binding != DataId::RemainingCapacityAh &&
      spec.binding != DataId::RemainingRangeKm) {
    ok = false;
  }
  if (!ok) { setError(result, lineNumber, "invalid widget field(s)"); return false; }
  package.widgets[package.widgetCount++] = spec;
  return true;
}

bool parseAsset(char **fields, size_t count, Package &package) {
  if (package.version < 2U || count != 7U || package.assetCount >= MaxAssets) return false;
  if (findAssetMutable(package, fields[1]) != nullptr) return false;
  AssetSpec spec;
  if (!copyToken(fields[1], spec.name, sizeof(spec.name))) return false;
  long value = 0;
  const long maxW = package.version >= 3U ? AppConfig::Display::Width : MaxStateIconDimension;
  const long maxH = package.version >= 3U ? AppConfig::Display::Height : MaxStateIconDimension;
  if (!parseLong(fields[2], 1, maxW, value)) return false;
  spec.width = static_cast<uint16_t>(value);
  if (!parseLong(fields[3], 1, maxH, value)) return false;
  spec.height = static_cast<uint16_t>(value);
  size_t bytesPerPixel = 0U;
  if (strcasecmp(fields[4], "RGB565A8") == 0) {
    spec.format = AssetFormat::RGB565A8;
    bytesPerPixel = 3U;
  } else if (package.version >= 7U && strcasecmp(fields[4], "RGB565") == 0) {
    spec.format = AssetFormat::RGB565;
    bytesPerPixel = 2U;
  } else if (package.version >= 3U && strcasecmp(fields[4], "A8") == 0) {
    spec.format = AssetFormat::Alpha8;
    bytesPerPixel = 1U;
  } else {
    return false;
  }
  const size_t expected = static_cast<size_t>(spec.width) * spec.height * bytesPerPixel;
  if (expected == 0U || expected > MaxRuntimeAssetBytes) return false;
  if (!parseLong(fields[5], 1, static_cast<long>(MaxRuntimeAssetBytes), value) || static_cast<size_t>(value) != expected) return false;
  spec.byteCount = static_cast<uint32_t>(value);
  if (!parseUnsignedHex(fields[6], spec.expectedCrc32)) return false;
  size_t totalAssetBytes = spec.byteCount;
  for (uint16_t i = 0U; i < package.assetCount; ++i) totalAssetBytes += package.assets[i].byteCount;
  if (totalAssetBytes > MaxRuntimeAssetBytes) return false;
  package.assets[package.assetCount++] = spec;
  return true;
}

bool parseAssetData(char **fields, size_t count, Package &package) {
  if (package.version < 2U || count != 3U) return false;
  AssetSpec *asset = findAssetMutable(package, fields[1]);
  if (asset == nullptr) return false;
  const size_t chars = strlen(fields[2]);
  if (chars == 0U || (chars & 1U) != 0U) return false;
  const size_t bytes = chars / 2U;
  if (asset->receivedBytes + bytes > asset->byteCount) return false;
  for (size_t i = 0U; i < chars; i += 2U) {
    const int high = hexValue(fields[2][i]);
    const int low = hexValue(fields[2][i + 1U]);
    if (high < 0 || low < 0) return false;
    const uint8_t value = static_cast<uint8_t>((high << 4) | low);
    asset->runningCrc32 = crc32Update(asset->runningCrc32, value);
    ++asset->receivedBytes;
#if defined(ARDUINO)
    if ((i & 0x7FU) == 0U) delay(0);
#endif
  }
  return true;
}


int base64Value(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return 26 + c - 'a';
  if (c >= '0' && c <= '9') return 52 + c - '0';
  if (c == '+') return 62;
  if (c == '/') return 63;
  return -1;
}

bool appendAssetByte(AssetSpec &asset, uint8_t value) {
  if (asset.receivedBytes >= asset.byteCount) return false;
  asset.runningCrc32 = crc32Update(asset.runningCrc32, value);
  ++asset.receivedBytes;
  return true;
}

bool parseAssetBase64(char **fields, size_t count, Package &package) {
  if (package.version < 7U || count != 3U) return false;
  AssetSpec *asset = findAssetMutable(package, fields[1]);
  if (asset == nullptr || fields[2] == nullptr) return false;
  const char *text = fields[2];
  const size_t chars = strlen(text);
  if (chars == 0U || (chars & 3U) != 0U) return false;
  for (size_t i = 0U; i < chars; i += 4U) {
    const char c0 = text[i];
    const char c1 = text[i + 1U];
    const char c2 = text[i + 2U];
    const char c3 = text[i + 3U];
    const int v0 = base64Value(c0);
    const int v1 = base64Value(c1);
    if (v0 < 0 || v1 < 0) return false;
    const bool pad2 = c2 == '=';
    const bool pad3 = c3 == '=';
    const int v2 = pad2 ? 0 : base64Value(c2);
    const int v3 = pad3 ? 0 : base64Value(c3);
    if ((!pad2 && v2 < 0) || (!pad3 && v3 < 0)) return false;
    if (pad2 && !pad3) return false;
    if ((pad2 || pad3) && i + 4U != chars) return false;
    if (!appendAssetByte(*asset, static_cast<uint8_t>((v0 << 2) | (v1 >> 4)))) return false;
    if (!pad2) {
      if (!appendAssetByte(*asset, static_cast<uint8_t>(((v1 & 0x0F) << 4) | (v2 >> 2)))) return false;
    }
    if (!pad3) {
      if (!appendAssetByte(*asset, static_cast<uint8_t>(((v2 & 0x03) << 6) | v3))) return false;
    }
  }
  return true;
}

bool validateAssets(const Package &package) {
  for (uint16_t i = 0U; i < package.assetCount; ++i) {
    const AssetSpec &asset = package.assets[i];
    if (asset.receivedBytes != asset.byteCount || (asset.runningCrc32 ^ 0xFFFFFFFFU) != asset.expectedCrc32) return false;
  }
  for (uint16_t i = 0U; i < package.widgetCount; ++i) {
    const WidgetSpec &widget = package.widgets[i];
    if (widget.type == WidgetType::StateImage) {
      const AssetSpec *on = findAsset(package, widget.onAsset);
      const AssetSpec *off = findAsset(package, widget.offAsset);
      if (on == nullptr || off == nullptr || on->width != static_cast<uint16_t>(widget.width) || on->height != static_cast<uint16_t>(widget.height) || off->width != static_cast<uint16_t>(widget.width) || off->height != static_cast<uint16_t>(widget.height)) return false;
      if (on->format != off->format) return false;
      if (on->format != AssetFormat::RGB565A8 && on->format != AssetFormat::RGB565 && on->format != AssetFormat::Alpha8) return false;
      continue;
    }
    if (widget.type == WidgetType::GlyphValue) {
      const AssetSpec *asset = findAsset(package, widget.asset);
      if (asset == nullptr || asset->format != AssetFormat::Alpha8 || widget.glyphCount == 0U || widget.glyphColumns == 0U) return false;
      const uint16_t rows = static_cast<uint16_t>((widget.glyphCount + widget.glyphColumns - 1U) / widget.glyphColumns);
      if (asset->width != static_cast<uint16_t>(widget.glyphCellWidth) * widget.glyphColumns) return false;
      if (asset->height != rows * static_cast<uint16_t>(widget.height)) return false;
      continue;
    }
    if (widget.type == WidgetType::Image || widget.type == WidgetType::TextImage || widget.type == WidgetType::Shape) {
      const AssetSpec *asset = findAsset(package, widget.asset);
      if (asset == nullptr || asset->width != static_cast<uint16_t>(widget.width) || asset->height != static_cast<uint16_t>(widget.height)) return false;
      if (widget.type == WidgetType::TextImage && asset->format != AssetFormat::Alpha8) return false;
      if (widget.type == WidgetType::Image && asset->format != AssetFormat::RGB565A8 && asset->format != AssetFormat::RGB565) return false;
      if (widget.type == WidgetType::Shape && asset->format != AssetFormat::RGB565A8) return false;
    }
  }
  return true;
}

}  // namespace

bool parseDataId(const char *text, DataId &id) {
  if (text == nullptr) return false;
#define MAP_BINDING(token, value) if (strcasecmp(text, token) == 0) { id = DataId::value; return true; }
  MAP_BINDING("NONE", None) MAP_BINDING("SOC", Soc) MAP_BINDING("SOH", Soh)
  MAP_BINDING("VOLTAGE", TotalVoltage) MAP_BINDING("CURRENT", Current) MAP_BINDING("POWER", Power)
  MAP_BINDING("REMAINING_AH", RemainingCapacityAh) MAP_BINDING("TOTAL_AH", TotalCapacityAh) MAP_BINDING("RANGE_KM", RemainingRangeKm)
  MAP_BINDING("MOS_TEMP", MosTemperature) MAP_BINDING("DELTA_V", DeltaCellVoltage) MAP_BINDING("MAX_CELL_V", MaxCellVoltage)
  MAP_BINDING("MIN_CELL_V", MinCellVoltage) MAP_BINDING("AVG_CELL_V", AverageCellVoltage) MAP_BINDING("CYCLE_COUNT", CycleCount)
  MAP_BINDING("CELL_COUNT", CellCount) MAP_BINDING("MAX_CELL_INDEX", MaxCellIndex) MAP_BINDING("MIN_CELL_INDEX", MinCellIndex)
  MAP_BINDING("CHARGE_MOS", ChargeMos) MAP_BINDING("DISCHARGE_MOS", DischargeMos) MAP_BINDING("BALANCER", BalancerStatus) MAP_BINDING("CONNECTED", Connected)
#undef MAP_BINDING
  return false;
}

bool parseFontId(const char *text, FontId &id) {
  if (text == nullptr) return false;
  if (strcasecmp(text, "M14") == 0) id = FontId::Montserrat14;
  else if (strcasecmp(text, "M16") == 0) id = FontId::Montserrat16;
  else if (strcasecmp(text, "M22") == 0) id = FontId::Montserrat22;
  else if (strcasecmp(text, "M32") == 0) id = FontId::Montserrat32;
  else if (strcasecmp(text, "M48") == 0) id = FontId::Montserrat48;
  else if (strcasecmp(text, "CN16") == 0) id = FontId::Chinese16;
  else return false;
  return true;
}

bool parseAlignId(const char *text, AlignId &id) {
  if (text == nullptr) return false;
  if (strcasecmp(text, "L") == 0 || strcasecmp(text, "LEFT") == 0) id = AlignId::Left;
  else if (strcasecmp(text, "C") == 0 || strcasecmp(text, "CENTER") == 0) id = AlignId::Center;
  else if (strcasecmp(text, "R") == 0 || strcasecmp(text, "RIGHT") == 0) id = AlignId::Right;
  else return false;
  return true;
}

bool isSwitchBinding(DataId id) { return id == DataId::ChargeMos || id == DataId::DischargeMos || id == DataId::Connected; }

const AssetSpec *findAsset(const Package &package, const char *name) {
  if (name == nullptr) return nullptr;
  for (uint16_t i = 0U; i < package.assetCount; ++i) {
    if (strcmp(package.assets[i].name, name) == 0) return &package.assets[i];
  }
  return nullptr;
}

const char *widgetTypeName(WidgetType type) {
  switch (type) {
    case WidgetType::Label: return "LABEL"; case WidgetType::Rect: return "RECT"; case WidgetType::Line: return "LINE";
    case WidgetType::Arc: return "ARC"; case WidgetType::Bar: return "BAR"; case WidgetType::StateImage: return "STATEIMG";
    case WidgetType::Image: return "IMAGE"; case WidgetType::TextImage: return "TEXTIMG"; case WidgetType::Shape: return "SHAPE"; case WidgetType::VectorShape: return "VSHAPE"; case WidgetType::GlyphValue: return "GLYPHVAL"; default: return "UNKNOWN";
  }
}

const char *bindingName(DataId id) {
  switch (id) {
    case DataId::None: return "NONE"; case DataId::Soc: return "SOC"; case DataId::Soh: return "SOH";
    case DataId::TotalVoltage: return "VOLTAGE"; case DataId::Current: return "CURRENT"; case DataId::Power: return "POWER";
    case DataId::RemainingCapacityAh: return "REMAINING_AH"; case DataId::TotalCapacityAh: return "TOTAL_AH"; case DataId::RemainingRangeKm: return "RANGE_KM";
    case DataId::MosTemperature: return "MOS_TEMP"; case DataId::DeltaCellVoltage: return "DELTA_V"; case DataId::MaxCellVoltage: return "MAX_CELL_V";
    case DataId::MinCellVoltage: return "MIN_CELL_V"; case DataId::AverageCellVoltage: return "AVG_CELL_V"; case DataId::CycleCount: return "CYCLE_COUNT";
    case DataId::CellCount: return "CELL_COUNT"; case DataId::MaxCellIndex: return "MAX_CELL_INDEX"; case DataId::MinCellIndex: return "MIN_CELL_INDEX";
    case DataId::ChargeMos: return "CHARGE_MOS"; case DataId::DischargeMos: return "DISCHARGE_MOS"; case DataId::BalancerStatus: return "BALANCER";
    case DataId::Connected: return "CONNECTED"; default: return "NONE";
  }
}

bool parse(Stream &stream, Package &package, ParseResult &result) {
  package.version = ProtocolVersion;
  package.width = AppConfig::Display::Width;
  package.height = AppConfig::Display::Height;
  package.backgroundColor = 0x000000;
  package.title[0] = '\0';
  package.widgetCount = 0U;
  package.assetCount = 0U;
  result = ParseResult{};
  char line[MaxLineLength + 1] = {};
  uint16_t lineNumber = 0U;
  bool headerSeen = false;
  while (stream.available()) {
    const size_t length = stream.readBytesUntil('\n', line, MaxLineLength);
    ++lineNumber;
    if (length >= MaxLineLength && stream.peek() != '\n' && stream.available()) { setError(result, lineNumber, "line exceeds maximum length"); return false; }
    line[length] = '\0';
    trimRight(line);
    char *content = trimLeft(line);
#if defined(ARDUINO)
    delay(0);
#endif
    if (*content == '\0' || *content == '#') continue;
    char *fields[kMaxFields] = {};
    const size_t count = splitFields(content, fields, kMaxFields);
    if (count == 0U || count > kMaxFields) {
      setError(result, lineNumber, "too many fields on line");
      return false;
    }
    if (!headerSeen) {
      if (!parseHeader(fields, count, package, result, lineNumber)) return false;
      headerSeen = true;
      continue;
    }
    bool ok = false;
    if (strcasecmp(fields[0], "ASSET") == 0) ok = parseAsset(fields, count, package);
    else if (strcasecmp(fields[0], "ASSETDATA") == 0) ok = parseAssetData(fields, count, package);
    else if (strcasecmp(fields[0], "ASSETB64") == 0) ok = parseAssetBase64(fields, count, package);
    else {
      if (!parseWidget(fields, count, package, result, lineNumber)) return false;
      continue;
    }
    if (!ok) { setError(result, lineNumber, "invalid asset field(s)"); return false; }
  }
  if (!headerSeen) { setError(result, 0, "missing BMSUI header"); return false; }
  if (package.widgetCount == 0U) { setError(result, lineNumber, "package contains no widgets"); return false; }
  if (!validateAssets(package)) { setError(result, lineNumber, "asset validation failed"); return false; }
  result.ok = true;
  result.line = 0;
  snprintf(result.message, sizeof(result.message), "ok");
  return true;
}

}  // namespace BmsUi
