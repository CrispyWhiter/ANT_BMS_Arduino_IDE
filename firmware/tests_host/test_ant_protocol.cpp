#include <assert.h>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "../src/protocol/ant_protocol.h"

static uint32_t g_now = 1000U;
uint32_t millis() { return g_now; }

namespace DiagnosticLog {
bool begin() { return true; }
void write(const char *) {}
void printf(const char *, ...) {}
}

static BmsData g_data{};
static int g_status_count = 0;

static void onStatus(const BmsData &data) {
  g_data = data;
  ++g_status_count;
}

static void onInfo(const char *, const char *) {}

static void writeU16LE(std::vector<uint8_t> &frame, size_t index, uint16_t value) {
  frame[index] = static_cast<uint8_t>(value & 0xFFU);
  frame[index + 1U] = static_cast<uint8_t>(value >> 8U);
}

static void writeI16LE(std::vector<uint8_t> &frame, size_t index, int16_t value) {
  writeU16LE(frame, index, static_cast<uint16_t>(value));
}

static void writeU32LE(std::vector<uint8_t> &frame, size_t index, uint32_t value) {
  frame[index] = static_cast<uint8_t>(value & 0xFFU);
  frame[index + 1U] = static_cast<uint8_t>((value >> 8U) & 0xFFU);
  frame[index + 2U] = static_cast<uint8_t>((value >> 16U) & 0xFFU);
  frame[index + 3U] = static_cast<uint8_t>((value >> 24U) & 0xFFU);
}

static uint16_t crc16Modbus(const uint8_t *data, size_t length) {
  uint16_t crc = 0xFFFFU;
  for (size_t i = 0U; i < length; ++i) {
    crc ^= data[i];
    for (uint8_t bit = 0U; bit < 8U; ++bit) {
      crc = (crc & 1U) ? static_cast<uint16_t>((crc >> 1U) ^ 0xA001U)
                       : static_cast<uint16_t>(crc >> 1U);
    }
  }
  return crc;
}

static std::vector<uint8_t> makeStatusFrame(int16_t rawCurrentDeciA) {

  std::vector<uint8_t> frame(118U, 0U);
  frame[0] = 0x7EU;
  frame[1] = 0xA1U;
  frame[2] = 0x11U;
  frame[5] = 108U;
  frame[8] = 0U;
  frame[9] = 1U;

  writeU16LE(frame, 34U, 3700U);
  writeU16LE(frame, 40U, 7200U);
  writeI16LE(frame, 42U, rawCurrentDeciA);
  writeU16LE(frame, 44U, 75U);
  writeU16LE(frame, 46U, 100U);
  frame[48U] = 1U;
  frame[49U] = 1U;
  writeU32LE(frame, 52U, 70000000U);
  writeU32LE(frame, 56U, 35000000U);
  writeU32LE(frame, 60U, 70000U);
  writeU32LE(frame, 64U, 720U);
  writeU16LE(frame, 76U, 3700U);
  writeU16LE(frame, 78U, 1U);
  writeU16LE(frame, 80U, 3700U);
  writeU16LE(frame, 82U, 1U);
  writeU16LE(frame, 84U, 0U);
  writeU16LE(frame, 86U, 3700U);
  writeU32LE(frame, 108U, 100U);
  writeU32LE(frame, 112U, 0U);

  const uint16_t crc = crc16Modbus(frame.data() + 1U, frame.size() - 5U);
  writeU16LE(frame, frame.size() - 4U, crc);
  frame[frame.size() - 2U] = 0xAAU;
  frame[frame.size() - 1U] = 0x55U;
  return frame;
}

int main() {
  AntProtocolDecoder decoder(onStatus, onInfo);

  const auto discharge = makeStatusFrame(125);
  decoder.feed(discharge.data(), discharge.size());
  assert(g_status_count == 1);
  assert(g_data.valid);
  assert(std::fabs(g_data.current + 12.5f) < 0.001f);
  assert(g_data.power == -900);

  g_now += 3000U;
  const auto charge = makeStatusFrame(-40);
  decoder.feed(charge.data(), charge.size());
  assert(g_status_count == 2);
  assert(std::fabs(g_data.current - 4.0f) < 0.001f);
  assert(g_data.power == 288);

  std::puts("ANT protocol polarity host tests passed");
  return 0;
}
