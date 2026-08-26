#pragma once

#include <Arduino.h>
#include <Stream.h>

#include "bmsui_protocol.h"

namespace BmsUi {

bool parse(Stream &stream, Package &package, ParseResult &result);

bool parseDataId(const char *text, DataId &id);
bool parseFontId(const char *text, FontId &id);
bool parseAlignId(const char *text, AlignId &id);

}  // namespace BmsUi
