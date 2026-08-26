#pragma once

#include <Arduino.h>
#include <string.h>
#include <strings.h>

enum class BmsType : uint8_t {
  Ant = 0,
  Jikong = 1,
  Jiabaida = 2,
  Yanyang = 3,
};

namespace BmsTypeInfo {

inline const char *key(BmsType type) {
  switch (type) {
    case BmsType::Jikong: return "jikong";
    case BmsType::Jiabaida: return "jiabaida";
    case BmsType::Yanyang: return "yanyang";
    case BmsType::Ant:
    default: return "ant";
  }
}

inline const char *displayName(BmsType type) {
  switch (type) {
    case BmsType::Jikong: return "极空保护板";
    case BmsType::Jiabaida: return "嘉佰达保护板";
    case BmsType::Yanyang: return "彦阳保护板";
    case BmsType::Ant:
    default: return "蚂蚁保护板";
  }
}

inline const char *defaultDeviceName(BmsType type) {
  switch (type) {
    case BmsType::Jikong: return "JK BMS";
    case BmsType::Jiabaida: return "JBD BMS";
    case BmsType::Yanyang: return "YY BMS";
    case BmsType::Ant:
    default: return "ANT BMS";
  }
}

inline bool parse(const char *value, BmsType &output) {
  if (value == nullptr || value[0] == '\0') return false;

  if (strcasecmp(value, "ant") == 0 ||
      strcasecmp(value, "ant_bms") == 0 ||
      strcmp(value, "蚂蚁") == 0) {
    output = BmsType::Ant;
    return true;
  }

  if (strcasecmp(value, "jikong") == 0 ||
      strcasecmp(value, "jk") == 0 ||
      strcasecmp(value, "jkbms") == 0 ||
      strcasecmp(value, "jk_bms") == 0 ||
      strcmp(value, "极空") == 0) {
    output = BmsType::Jikong;
    return true;
  }

  if (strcasecmp(value, "jiabaida") == 0 ||
      strcasecmp(value, "jbd") == 0 ||
      strcasecmp(value, "xiaoxiang") == 0 ||
      strcasecmp(value, "jbd_bms") == 0 ||
      strcmp(value, "嘉佰达") == 0 ||
      strcmp(value, "小象") == 0) {
    output = BmsType::Jiabaida;
    return true;
  }

  if (strcasecmp(value, "yanyang") == 0 ||
      strcasecmp(value, "yy") == 0 ||
      strcasecmp(value, "yybms") == 0 ||
      strcasecmp(value, "yy_bms") == 0 ||
      strcmp(value, "彦阳") == 0) {
    output = BmsType::Yanyang;
    return true;
  }

  return false;
}

inline bool isValidRaw(uint8_t raw) {
  return raw <= static_cast<uint8_t>(BmsType::Yanyang);
}

}
