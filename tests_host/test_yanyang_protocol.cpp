#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "bms_type.h"
#include "protocol/yanyang_protocol.h"

static uint32_t g_now = 2000;
uint32_t millis() { return g_now; }

namespace DiagnosticLog {
bool begin() { return true; }
void write(const char *) {}
void printf(const char *, ...) {}
}

static BmsData g_data;
static int g_status_calls = 0;
static int g_info_calls = 0;
static char g_hw[17] = {};

static void onStatus(const BmsData &data) {
  g_data = data;
  ++g_status_calls;
}

static void onInfo(const char *hardware, const char *) {
  std::snprintf(g_hw, sizeof(g_hw), "%s", hardware == nullptr ? "" : hardware);
  ++g_info_calls;
}

static size_t regOffset(uint16_t reg) { return static_cast<size_t>(reg - 75U) * 2U; }
static void put16le(std::vector<uint8_t> &data, size_t offset, uint16_t value) {
  data[offset] = static_cast<uint8_t>(value & 0xFF);
  data[offset + 1] = static_cast<uint8_t>(value >> 8);
}
static void put32le(std::vector<uint8_t> &data, size_t offset, uint32_t value) {
  data[offset] = static_cast<uint8_t>(value & 0xFF);
  data[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xFF);
  data[offset + 2] = static_cast<uint8_t>((value >> 16) & 0xFF);
  data[offset + 3] = static_cast<uint8_t>((value >> 24) & 0xFF);
}

static std::vector<uint8_t> makeResponse() {
  std::vector<uint8_t> payload(184U, 0);
  payload[regOffset(75)] = 16;
  payload[regOffset(75) + 1] = 1;
  put32le(payload, regOffset(76), 53120U);
  put32le(payload, regOffset(78), static_cast<uint32_t>(-456));
  for (uint8_t i = 0; i < 16; ++i) put16le(payload, regOffset(81U + i), 3310U + i);
  payload[regOffset(112)] = 72;
  payload[regOffset(112) + 1] = 70;
  payload[regOffset(113)] = 69;
  payload[regOffset(113) + 1] = 68;
  put16le(payload, regOffset(118), 700U);
  put16le(payload, regOffset(119), 455U);
  payload[regOffset(120)] = 65;
  payload[regOffset(120) + 1] = 96;
  put32le(payload, regOffset(139), 0x00000005U);
  put32le(payload, regOffset(152), 0U);
  put32le(payload, regOffset(156), 0x00000123U);

  std::vector<uint8_t> frame(3U + payload.size() + 2U, 0);
  frame[0] = 1;
  frame[1] = 3;
  frame[2] = static_cast<uint8_t>(payload.size());
  std::copy(payload.begin(), payload.end(), frame.begin() + 3);
  const uint16_t crc = YanyangProtocolDecoder::crc16(frame.data(), frame.size() - 2U);
  frame[frame.size() - 2U] = static_cast<uint8_t>(crc & 0xFF);
  frame[frame.size() - 1U] = static_cast<uint8_t>(crc >> 8);
  return frame;
}

static void feedChunked(YanyangProtocolDecoder &decoder,
                        const std::vector<uint8_t> &frame) {
  size_t offset = 0;
  const size_t chunks[] = {2, 7, 20, 1, 64, 31};
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
  assert(BmsTypeInfo::parse("YYBMS", type) && type == BmsType::Yanyang);
  assert(BmsTypeInfo::parse("彦阳", type) && type == BmsType::Yanyang);

  uint8_t request[8] = {};
  YanyangProtocolDecoder::buildStatusRequest(1, request);
  assert(request[0] == 1 && request[1] == 3);
  assert(request[2] == 0 && request[3] == 75);
  assert(request[4] == 0 && request[5] == 92);
  const uint16_t expectedCrc = YanyangProtocolDecoder::crc16(request, 6);
  assert(request[6] == (expectedCrc & 0xFF) && request[7] == (expectedCrc >> 8));

  YanyangProtocolDecoder decoder(onStatus, onInfo);
  auto frame = makeResponse();
  std::vector<uint8_t> noisy = {0xFF, 0x00, 0x12};
  noisy.insert(noisy.end(), frame.begin(), frame.end());
  feedChunked(decoder, noisy);

  assert(g_status_calls == 1 && g_info_calls == 1);
  assert(std::strcmp(g_hw, "YY Modbus") == 0);
  assert(g_data.cellCount == 16 && g_data.batteryType == 1);
  assert(std::fabs(g_data.totalVoltage - 53.120f) < 0.001f);
  assert(std::fabs(g_data.current - 4.56f) < 0.001f);
  assert(std::fabs(g_data.totalCapacityAh - 70.0f) < 0.001f);
  assert(std::fabs(g_data.remainingCapacityAh - 45.5f) < 0.001f);
  assert(g_data.soc == 65 && g_data.soh == 96);
  assert(g_data.mosTemperature == 30 && g_data.balancerTemperature == 32);
  assert(g_data.temperatures[0] == 28 && g_data.temperatures[1] == 29);
  assert(g_data.chargeMos == 1 && g_data.dischargeMos == 1);
  assert(g_data.balanceMask == 5 && g_data.batteryStatus == 0x23);
  assert(g_data.minCell == 1 && g_data.maxCell == 16);

  auto bad = frame;
  bad[50] ^= 0x01;
  decoder.feed(bad.data(), bad.size());
  assert(g_status_calls == 1);

  std::puts("Yanyang protocol host tests passed");
  return 0;
}
