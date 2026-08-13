#include "jbd_protocol.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../core/diagnostic_log.h"
#include "../core/time_utils.h"

namespace {

constexpr uint8_t kStartByte = 0xDD;
constexpr uint8_t kReadMarker = 0xA5;
constexpr uint8_t kStopByte = 0x77;
constexpr uint8_t kCommandBasic = 0x03;
constexpr uint8_t kCommandCells = 0x04;
constexpr uint8_t kCommandHardware = 0x05;
constexpr size_t kMinimumFrameSize = 7;

uint16_t readU16BE(const uint8_t *data, size_t index) {
  return (static_cast<uint16_t>(data[index]) << 8U) |
         static_cast<uint16_t>(data[index + 1U]);
}

int16_t readI16BE(const uint8_t *data, size_t index) {
  return static_cast<int16_t>(readU16BE(data, index));
}

uint16_t checksumForRange(const uint8_t *data, size_t start, size_t endExclusive) {
  uint16_t sum = 0;
  for (size_t i = start; i < endExclusive; ++i) {
    sum = static_cast<uint16_t>(sum + data[i]);
  }
  return static_cast<uint16_t>(0U - sum);
}

bool finiteWithin(float value, float minimum, float maximum) {
  return isfinite(value) && value >= minimum && value <= maximum;
}

void copyPrintableText(char *destination,
                       size_t destinationSize,
                       const uint8_t *source,
                       size_t sourceLength) {
  if (destination == nullptr || destinationSize == 0U) return;
  size_t written = 0;
  for (size_t i = 0; i < sourceLength && written + 1U < destinationSize; ++i) {
    const unsigned char c = source[i];
    if (c == 0U || c == 0xFFU) break;
    destination[written++] = isprint(c) ? static_cast<char>(c) : ' ';
  }
  while (written > 0U && isspace(static_cast<unsigned char>(destination[written - 1U]))) {
    --written;
  }
  destination[written] = '\0';
}

}

JbdProtocolDecoder::JbdProtocolDecoder(StatusCallback statusCallback,
                                       DeviceInfoCallback deviceInfoCallback)
    : statusCallback_(statusCallback),
      deviceInfoCallback_(deviceInfoCallback) {}

void JbdProtocolDecoder::reset() {
  frameLength_ = 0;
  lastChunkAt_ = 0;
  latest_ = BmsData{};
  haveBasic_ = false;
}

void JbdProtocolDecoder::buildReadCommand(uint8_t command, uint8_t output[7]) {
  if (output == nullptr) return;
  output[0] = kStartByte;
  output[1] = kReadMarker;
  output[2] = command;
  output[3] = 0x00;
  const uint16_t checksum = checksumForRange(output, 2U, 4U);
  output[4] = static_cast<uint8_t>(checksum >> 8U);
  output[5] = static_cast<uint8_t>(checksum & 0xFFU);
  output[6] = kStopByte;
}

void JbdProtocolDecoder::feed(const uint8_t *data, size_t length) {
  if (data == nullptr || length == 0U) return;

  const uint32_t now = millis();
  if (frameLength_ > 0U &&
      TimeUtils::elapsedSince(now, lastChunkAt_) >
          AppConfig::Protocol::FrameAssemblyTimeoutMs) {
    DiagnosticLog::write("JBD frame assembly timeout; buffer cleared.\n");
    frameLength_ = 0U;
  }
  lastChunkAt_ = now;

  if (length > sizeof(frameBuffer_)) {
    data += length - sizeof(frameBuffer_);
    length = sizeof(frameBuffer_);
    frameLength_ = 0U;
  }

  if (frameLength_ + length > sizeof(frameBuffer_)) {
    DiagnosticLog::write("JBD frame buffer overflow; resynchronizing.\n");
    frameLength_ = 0U;
  }

  memcpy(frameBuffer_ + frameLength_, data, length);
  frameLength_ += length;
  while (processBufferedFrames()) {}
}

bool JbdProtocolDecoder::processBufferedFrames() {

  size_t start = 0U;
  while (start < frameLength_ && frameBuffer_[start] != kStartByte) ++start;
  if (start > 0U) {
    memmove(frameBuffer_, frameBuffer_ + start, frameLength_ - start);
    frameLength_ -= start;
  }
  if (frameLength_ < 4U) return false;

  const size_t payloadLength = frameBuffer_[3];
  const size_t frameSize = payloadLength + kMinimumFrameSize;
  if (frameSize > sizeof(frameBuffer_)) {
    memmove(frameBuffer_, frameBuffer_ + 1U, frameLength_ - 1U);
    --frameLength_;
    return true;
  }
  if (frameLength_ < frameSize) return false;

  if (frameBuffer_[frameSize - 1U] != kStopByte) {

    memmove(frameBuffer_, frameBuffer_ + 1U, frameLength_ - 1U);
    --frameLength_;
    return true;
  }

  const uint16_t storedChecksum =
      (static_cast<uint16_t>(frameBuffer_[frameSize - 3U]) << 8U) |
      frameBuffer_[frameSize - 2U];

  const uint16_t calculatedChecksum = checksumForRange(
      frameBuffer_, 2U, frameSize - 3U);
  if (storedChecksum == calculatedChecksum) {
    processFrame(frameBuffer_, frameSize);
  } else {
    DiagnosticLog::printf(
        "JBD checksum rejected: stored=0x%04X calculated=0x%04X.\n",
        static_cast<unsigned>(storedChecksum),
        static_cast<unsigned>(calculatedChecksum));
  }

  memmove(frameBuffer_, frameBuffer_ + frameSize, frameLength_ - frameSize);
  frameLength_ -= frameSize;
  return frameLength_ > 0U;
}

void JbdProtocolDecoder::processFrame(const uint8_t *frame, size_t length) {
  if (frame == nullptr || length < kMinimumFrameSize) return;
  const uint8_t command = frame[1];
  const uint8_t status = frame[2];
  const size_t payloadLength = frame[3];
  if (payloadLength + kMinimumFrameSize != length) return;
  if (status != 0x00U) {
    DiagnosticLog::printf("JBD command 0x%02X returned status 0x%02X.\n",
                          command, status);
    return;
  }

  const uint8_t *payload = frame + 4U;
  switch (command) {
    case kCommandBasic:
      if (parseBasicStatus(payload, payloadLength)) publishStatus();
      break;
    case kCommandCells:
      if (parseCellVoltages(payload, payloadLength) && haveBasic_) publishStatus();
      break;
    case kCommandHardware: {
      char hardware[17] = {};
      copyPrintableText(hardware, sizeof(hardware), payload, payloadLength);
      if (hardware[0] == '\0') snprintf(hardware, sizeof(hardware), "JBD BMS");
      if (deviceInfoCallback_ != nullptr) {
        deviceInfoCallback_(hardware, latest_.softwareVersion);
      }
      break;
    }
    default:
      break;
  }
}

bool JbdProtocolDecoder::parseBasicStatus(const uint8_t *payload, size_t length) {

  if (payload == nullptr || length < 23U) return false;

  BmsData parsed = latest_;
  parsed.valid = true;
  parsed.updatedAt = millis();
  parsed.totalVoltage = readU16BE(payload, 0U) * 0.01f;
  parsed.current = readI16BE(payload, 2U) * 0.01f;
  parsed.remainingCapacityAh = readU16BE(payload, 4U) * 0.01f;
  parsed.totalCapacityAh = readU16BE(payload, 6U) * 0.01f;
  parsed.reportedCycleCount = readU16BE(payload, 8U);
  const uint16_t protection = readU16BE(payload, 16U);
  parsed.batteryStatus = static_cast<uint8_t>(protection & 0xFFU);

  const uint8_t softwareRaw = payload[18U];
  snprintf(parsed.softwareVersion,
           sizeof(parsed.softwareVersion),
           "%u.%u",
           static_cast<unsigned>(softwareRaw >> 4U),
           static_cast<unsigned>(softwareRaw & 0x0FU));
  parsed.soc = payload[19U] <= 100U ? payload[19U] : 100U;
  const uint8_t fet = payload[20U];
  parsed.chargeMos = (fet & 0x01U) != 0U;
  parsed.dischargeMos = (fet & 0x02U) != 0U;

  const uint8_t declaredCells = payload[21U];
  if (declaredCells > 0U) {
    parsed.cellCount = min(declaredCells, AppConfig::Protocol::MaxCellCount);
  }

  const uint8_t declaredTemperatures = payload[22U];
  const uint8_t availableTemperatures = static_cast<uint8_t>((length - 23U) / 2U);
  parsed.temperatureCount = min(
      min(declaredTemperatures, availableTemperatures),
      AppConfig::Protocol::MaxTemperatureCount);
  for (uint8_t i = 0U; i < parsed.temperatureCount; ++i) {
    const int32_t deciKelvin = readU16BE(payload, 23U + i * 2U);
    const int32_t celsius = (deciKelvin - 2731) / 10;
    const int32_t clamped = celsius < AppConfig::Protocol::MinTemperatureC
                                ? AppConfig::Protocol::MinTemperatureC
                                : (celsius > AppConfig::Protocol::MaxTemperatureC
                                       ? AppConfig::Protocol::MaxTemperatureC
                                       : celsius);
    parsed.temperatures[i] = static_cast<int16_t>(clamped);
  }
  for (uint8_t i = parsed.temperatureCount;
       i < AppConfig::Protocol::MaxTemperatureCount;
       ++i) {
    parsed.temperatures[i] = 0;
  }
  if (parsed.temperatureCount > 0U) parsed.mosTemperature = parsed.temperatures[0];

  parsed.balanceMask = static_cast<uint32_t>(readU16BE(payload, 12U)) |
                       (static_cast<uint32_t>(readU16BE(payload, 14U)) << 16U);
  parsed.balancerStatus = parsed.balanceMask != 0U;
  parsed.power = static_cast<int32_t>(lroundf(parsed.totalVoltage * parsed.current));

  if (!finiteWithin(parsed.totalVoltage, 0.0f, AppConfig::Protocol::MaxPackVoltage) ||
      !finiteWithin(fabsf(parsed.current), 0.0f, AppConfig::Protocol::MaxAbsCurrent) ||
      !finiteWithin(parsed.totalCapacityAh, 0.0f, AppConfig::Protocol::MaxCapacityAh) ||
      !finiteWithin(parsed.remainingCapacityAh, 0.0f, AppConfig::Protocol::MaxCapacityAh)) {
    DiagnosticLog::write("JBD basic frame rejected by physical range checks.\n");
    return false;
  }

  latest_ = parsed;
  haveBasic_ = true;
  return true;
}

bool JbdProtocolDecoder::parseCellVoltages(const uint8_t *payload, size_t length) {
  if (payload == nullptr || length < 2U || (length & 1U) != 0U) return false;
  const uint8_t count = min(
      static_cast<uint8_t>(length / 2U),
      AppConfig::Protocol::MaxCellCount);
  if (count == 0U) return false;

  for (uint8_t i = 0U; i < count; ++i) {
    latest_.cells[i] = readU16BE(payload, i * 2U) * 0.001f;
  }
  for (uint8_t i = count; i < AppConfig::Protocol::MaxCellCount; ++i) {
    latest_.cells[i] = 0.0f;
  }
  latest_.cellCount = count;
  updateCellStatistics();
  return true;
}

void JbdProtocolDecoder::updateCellStatistics() {
  float minimum = 0.0f;
  float maximum = 0.0f;
  float sum = 0.0f;
  uint8_t validCount = 0U;
  uint16_t minCell = 0U;
  uint16_t maxCell = 0U;

  for (uint8_t i = 0U; i < latest_.cellCount; ++i) {
    const float voltage = latest_.cells[i];
    if (!finiteWithin(voltage,
                      AppConfig::Protocol::MinCellVoltage,
                      AppConfig::Protocol::MaxCellVoltage)) {
      continue;
    }
    if (validCount == 0U || voltage < minimum) {
      minimum = voltage;
      minCell = i + 1U;
    }
    if (validCount == 0U || voltage > maximum) {
      maximum = voltage;
      maxCell = i + 1U;
    }
    sum += voltage;
    ++validCount;
  }

  latest_.minCellVoltage = validCount == 0U ? 0.0f : minimum;
  latest_.maxCellVoltage = validCount == 0U ? 0.0f : maximum;
  latest_.minCell = minCell;
  latest_.maxCell = maxCell;
  latest_.deltaCellVoltage = validCount == 0U ? 0.0f : maximum - minimum;
  latest_.averageCellVoltage = validCount == 0U ? 0.0f : sum / validCount;
}

void JbdProtocolDecoder::publishStatus() {
  if (!haveBasic_ || statusCallback_ == nullptr) return;
  latest_.valid = true;
  latest_.updatedAt = millis();
  statusCallback_(latest_);
}
