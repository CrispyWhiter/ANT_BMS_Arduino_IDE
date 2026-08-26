#pragma once

#include <Arduino.h>
#include <stdint.h>

namespace RuntimeProfile {

uint32_t entropyFold();
uint16_t spanCount();
uint32_t spanAt(uint16_t index);
uint16_t settleWindowMs();
uint32_t sessionMask(uint32_t value);
uint16_t linkBiasMs();

}
