#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "bms_type.h"
#include "protocol/jbd_protocol.h"

static uint32_t g_now = 1000;
uint32_t millis() { return g_now; }

namespace DiagnosticLog {
bool begin() { return true; }
void write(const char *) {}
void printf(const char *, ...) {}
}

static BmsData g_data;
static int g_status_calls = 0;
static char g_hw[17] = {};
static char g_sw[17] = {};
static int g_info_calls = 0;

static void onStatus(const BmsData &data) {
  g_data = data;
  ++g_status_calls;
}

static void onInfo(const char *hardware, const char *software) {
  std::snprintf(g_hw, sizeof(g_hw), "%s", hardware == nullptr ? "" : hardware);
  std::snprintf(g_sw, sizeof(g_sw), "%s", software == nullptr ? "" : software);
  ++g_info_calls;
}

static void put16be(std::vector<uint8_t> &data, size_t index, uint16_t value) {
  data[index] = static_cast<uint8_t>(value >> 8);
  data[index + 1] = static_cast<uint8_t>(value & 0xFF);
}

static std::vector<uint8_t> response(uint8_t command,
                                     const std::vector<uint8_t> &payload) {
  std::vector<uint8_t> frame(payload.size() + 7U, 0);
  frame[0] = 0xDD;
  frame[1] = command;
  frame[2] = 0x00;
  frame[3] = static_cast<uint8_t>(payload.size());
  std::copy(payload.begin(), payload.end(), frame.begin() + 4);
  uint16_t sum = 0;
  for (size_t i = 2; i < frame.size() - 3U; ++i) sum += frame[i];
  const uint16_t checksum = static_cast<uint16_t>(0U - sum);
  frame[frame.size() - 3U] = static_cast<uint8_t>(checksum >> 8);
  frame[frame.size() - 2U] = static_cast<uint8_t>(checksum & 0xFF);
  frame.back() = 0x77;
  return frame;
}

static void feedChunked(JbdProtocolDecoder &decoder,
                        const std::vector<uint8_t> &frame) {
  size_t offset = 0;
  const size_t chunks[] = {1, 2, 5, 3, 11, 7};
  for (size_t chunk : chunks) {
    if (offset >= frame.size()) break;
    const size_t count = std::min(chunk, frame.size() - offset);
    decoder.feed(frame.data() + offset, count);
    offset += count;
  }
  if (offset < frame.size()) decoder.feed(frame.data() + offset, frame.size() - offset);
}

int main() {
  BmsType type = BmsType::Ant;
  assert(BmsTypeInfo::parse("JBD", type) && type == BmsType::Jiabaida);
  assert(BmsTypeInfo::parse("小象", type) && type == BmsType::Jiabaida);

  uint8_t command[7] = {};
  JbdProtocolDecoder::buildReadCommand(0x03, command);
  const uint8_t expected[] = {0xDD, 0xA5, 0x03, 0x00, 0xFF, 0xFD, 0x77};
  assert(std::memcmp(command, expected, sizeof(expected)) == 0);

  JbdProtocolDecoder decoder(onStatus, onInfo);
  std::vector<uint8_t> basic(27U, 0);
  put16be(basic, 0, 7215);
  put16be(basic, 2, static_cast<uint16_t>(-1234));
  put16be(basic, 4, 5234);
  put16be(basic, 6, 7000);
  put16be(basic, 8, 321);
  put16be(basic, 12, 0x0003);
  put16be(basic, 14, 0x0001);
  put16be(basic, 16, 0x0123);
  basic[18] = 0x21;
  basic[19] = 75;
  basic[20] = 0x03;
  basic[21] = 20;
  basic[22] = 2;
  put16be(basic, 23, 2981);
  put16be(basic, 25, 3031);

  auto basicFrame = response(0x03, basic);
  std::vector<uint8_t> noisy = {0x00, 0x12, 0xDD, 0x44};
  noisy.insert(noisy.end(), basicFrame.begin(), basicFrame.end());
  decoder.feed(noisy.data(), noisy.size());
  assert(g_status_calls == 1);
  assert(g_data.valid && g_data.soc == 75 && g_data.cellCount == 20);
  assert(std::fabs(g_data.totalVoltage - 72.15f) < 0.001f);
  assert(std::fabs(g_data.current + 12.34f) < 0.001f);
  assert(std::fabs(g_data.remainingCapacityAh - 52.34f) < 0.001f);
  assert(g_data.chargeMos == 1 && g_data.dischargeMos == 1);
  assert(g_data.temperatureCount == 2 && g_data.temperatures[0] == 25);
  assert(std::strcmp(g_data.softwareVersion, "2.1") == 0);

  std::vector<uint8_t> cells(40U, 0);
  for (size_t i = 0; i < 20U; ++i) put16be(cells, i * 2U, 3600U + i);
  feedChunked(decoder, response(0x04, cells));
  assert(g_status_calls == 2);
  assert(g_data.cellCount == 20 && g_data.minCell == 1 && g_data.maxCell == 20);
  assert(std::fabs(g_data.cells[0] - 3.600f) < 0.001f);
  assert(std::fabs(g_data.deltaCellVoltage - 0.019f) < 0.001f);

  const char *name = "JBD-SP20S001";
  std::vector<uint8_t> hardware(name, name + std::strlen(name));
  feedChunked(decoder, response(0x05, hardware));
  assert(g_info_calls == 1);
  assert(std::strcmp(g_hw, name) == 0);
  assert(std::strcmp(g_sw, "2.1") == 0);

  auto bad = basicFrame;
  bad[10] ^= 0x01;
  decoder.feed(bad.data(), bad.size());
  assert(g_status_calls == 2);

  std::puts("JBD protocol host tests passed");
  return 0;
}
