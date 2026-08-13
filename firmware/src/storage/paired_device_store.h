#pragma once

#include <Arduino.h>

#include "../bms_type.h"

namespace PairedDeviceStore {

bool load(BmsType &type,
          char *address,
          size_t addressSize,
          char *name,
          size_t nameSize);

bool save(BmsType type, const char *address, const char *name);

bool clear();

}
