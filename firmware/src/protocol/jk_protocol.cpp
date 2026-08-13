#include "jk_protocol.h"

#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "../core/diagnostic_log.h"
#include "../core/time_utils.h"

namespace {

constexpr uint8_t kHeader[] = {0x55, 0xAA, 0xEB, 0x90};
constexpr size_t kFrameSize = 300;
constexpr size_t kChecksumIndex = 299;
constexpr uint8_t kStatusFrameType = 0x02;
constexpr uint8_t kDeviceInfoFrameType = 0x03;

bool rangeValid(size_t index, size_t count, size_t length) {
  return index <= length && count <= length - index;
}

uint16_t readU16LE(const uint8_t *data, size_t index) {
  return static_cast<uint16_t>(data[index]) |
         (static_cast<uint16_t>(data[index + 1U]) << 8U);
}

int16_t readI16LE(const uint8_t *data, size_t index) {
  return static_cast<int16_t>(readU16LE(data, index));
}

uint32_t readU32LE(const uint8_t *data, size_t index) {
  return static_cast<uint32_t>(data[index]) |
         (static_cast<uint32_t>(data[index + 1U]) << 8U) |
         (static_cast<uint32_t>(data[index + 2U]) << 16U) |
         (static_cast<uint32_t>(data[index + 3U]) << 24U);
}

int32_t readI32LE(const uint8_t *data, size_t index) {
  return static_cast<int32_t>(readU32LE(data, index));
}

bool hasHeader(const uint8_t *data) {
  return data != nullptr && memcmp(data, kHeader, sizeof(kHeader)) == 0;
}

bool checksumValid(const uint8_t *frame, size_t length) {
  if (frame == nullptr || length != kFrameSize) return false;
  uint8_t sum = 0;
  for (size_t i = 0; i < kChecksumIndex; ++i) {
    sum = static_cast<uint8_t>(sum + frame[i]);
  }
  return sum == frame[kChecksumIndex];
}

bool finiteWithin(float value, float minimum, float maximum) {
  return isfinite(value) && value >= minimum && value <= maximum;
}

void copyProtocolText(char *destination,
                      size_t destinationSize,
                      const uint8_t *source,
                      size_t sourceLength) {
  if (destination == nullptr || destinationSize == 0U) return;
  const size_t count = min(sourceLength, destinationSize - 1U);
  memcpy(destination, source, count);
  destination[count] = '\0';

  for (size_t i = count; i > 0U; --i) {
    const unsigned char c = static_cast<unsigned char>(destination[i - 1U]);
    if (c == 0U || c == 0xFFU || isspace(c)) destination[i - 1U] = '\0';
    else break;
  }
}

uint8_t countBits(uint32_t value) {
  uint8_t count = 0;
  while (value != 0U) {
    count += static_cast<uint8_t>(value & 1U);
    value >>= 1U;
  }
  return count;
}

int hardwareMajor(const char *version) {
  if (version == nullptr) return -1;
  while (*version != '\0' && !isdigit(static_cast<unsigned char>(*version))) ++version;
  if (*version == '\0') return -1;
  return atoi(version);
}

}

JkProtocolDecoder::JkProtocolDecoder(StatusCallback statusCallback,
                                     DeviceInfoCallback deviceInfoCallback)
    : statusCallback_(statusCallback),
      deviceInfoCallback_(deviceInfoCallback) {}

void JkProtocolDecoder::reset() {
  frameLength_ = 0;
  lastChunkAt_ = 0;
  detectedVariant_ = Variant::Auto;
}

JkProtocolDecoder::Variant JkProtocolDecoder::detectedVariant() const {
  return detectedVariant_;
}

void JkProtocolDecoder::buildReadCommand(uint8_t command,
                                         uint8_t output[20]) {
  if (output == nullptr) return;
  memset(output, 0, 20U);
  output[0] = 0xAA;
  output[1] = 0x55;
  output[2] = 0x90;
  output[3] = 0xEB;
  output[4] = command;
  output[5] = 0x00;

  uint8_t checksum = 0;
  for (size_t i = 0; i < 19U; ++i) checksum = static_cast<uint8_t>(checksum + output[i]);
  output[19] = checksum;
}

void JkProtocolDecoder::feed(const uint8_t *data, size_t length) {
  if (data == nullptr || length == 0U) return;

  const uint32_t now = millis();
  if (frameLength_ > 0U &&
      TimeUtils::elapsedSince(now, lastChunkAt_) >
          AppConfig::Protocol::FrameAssemblyTimeoutMs) {
    DiagnosticLog::write("JK frame assembly timeout; buffer cleared.\n");
    frameLength_ = 0U;
  }
  lastChunkAt_ = now;

  if (length > sizeof(frameBuffer_)) {
    data += length - sizeof(frameBuffer_);
    length = sizeof(frameBuffer_);
    frameLength_ = 0U;
  }
  if (length > sizeof(frameBuffer_) - frameLength_) {
    DiagnosticLog::write("JK frame buffer overflow; resynchronizing.\n");
    frameLength_ = 0U;
  }

  memcpy(frameBuffer_ + frameLength_, data, length);
  frameLength_ += length;
  processBufferedFrames();
}

bool JkProtocolDecoder::processBufferedFrames() {
  bool processed = false;

  while (frameLength_ > 0U) {
    size_t headerIndex = 0U;
    while (headerIndex + sizeof(kHeader) <= frameLength_ &&
           memcmp(frameBuffer_ + headerIndex, kHeader, sizeof(kHeader)) != 0) {
      ++headerIndex;
    }

    if (headerIndex > 0U) {
      const size_t remaining = frameLength_ - headerIndex;
      memmove(frameBuffer_, frameBuffer_ + headerIndex, remaining);
      frameLength_ = remaining;
    }

    if (frameLength_ < sizeof(kHeader)) return processed;
    if (!hasHeader(frameBuffer_)) {

      const size_t keep = min(frameLength_, sizeof(kHeader) - 1U);
      memmove(frameBuffer_, frameBuffer_ + frameLength_ - keep, keep);
      frameLength_ = keep;
      return processed;
    }
    if (frameLength_ < kFrameSize) return processed;

    if (!checksumValid(frameBuffer_, kFrameSize)) {
      DiagnosticLog::write("JK frame checksum error; searching next header.\n");
      memmove(frameBuffer_, frameBuffer_ + 1U, frameLength_ - 1U);
      --frameLength_;
      continue;
    }

    processFrame(frameBuffer_, kFrameSize);
    processed = true;
    const size_t remaining = frameLength_ - kFrameSize;
    if (remaining > 0U) memmove(frameBuffer_, frameBuffer_ + kFrameSize, remaining);
    frameLength_ = remaining;
  }
  return processed;
}

void JkProtocolDecoder::processFrame(const uint8_t *frame, size_t length) {
  if (!hasHeader(frame) || length != kFrameSize) return;

  if (frame[4] == kStatusFrameType) {
    BmsData parsed;
    if (parseStatusFrame(frame, length, parsed) && statusCallback_ != nullptr) {
      statusCallback_(parsed);
    }
    return;
  }

  if (frame[4] == kDeviceInfoFrameType) {
    char hardware[17] = {};
    char software[17] = {};
    if (parseDeviceInfoFrame(frame, length,
                             hardware, sizeof(hardware),
                             software, sizeof(software)) &&
        deviceInfoCallback_ != nullptr) {
      deviceInfoCallback_(hardware, software);
    }
    return;
  }

  DiagnosticLog::printf("Unhandled JK frame type 0x%02X.\n", frame[4]);
}

JkProtocolDecoder::Candidate JkProtocolDecoder::parseStatusCandidate(
    const uint8_t *frame, Variant variant) const {
  Candidate candidate;
  candidate.variant = variant;
  BmsData &data = candidate.data;

  const bool is32 = variant == Variant::Jk02_32S;
  const uint8_t slotCount = is32 ? 32U : 24U;
  const size_t lateOffset = is32 ? 32U : 0U;

  float sum = 0.0f;
  float maximum = -1.0f;
  float minimum = AppConfig::Protocol::MaxCellVoltage + 1.0f;
  uint8_t validCells = 0U;
  uint8_t suspiciousCells = 0U;

  for (uint8_t i = 0; i < slotCount; ++i) {
    const uint16_t millivolts = readU16LE(frame, 6U + static_cast<size_t>(i) * 2U);
    const float voltage = static_cast<float>(millivolts) * 0.001f;
    data.cells[i] = voltage;
    if (millivolts == 0U) continue;
    if (!finiteWithin(voltage,
                      AppConfig::Protocol::MinCellVoltage,
                      AppConfig::Protocol::MaxCellVoltage)) {
      ++suspiciousCells;
      continue;
    }
    ++validCells;
    sum += voltage;
    if (voltage > maximum) {
      maximum = voltage;
      data.maxCell = i + 1U;
    }
    if (voltage < minimum) {
      minimum = voltage;
      data.minCell = i + 1U;
    }
  }

  const size_t maskOffset = is32 ? 70U : 54U;
  const uint32_t enabledMask = readU32LE(frame, maskOffset);
  const uint8_t enabledCells = countBits(enabledMask);

  const size_t voltageOffset = 118U + lateOffset;
  const size_t currentOffset = 126U + lateOffset;
  const size_t temp1Offset = 130U + lateOffset;
  const size_t temp2Offset = 132U + lateOffset;

  const size_t errorOffset = is32 ? 166U : 136U;
  const size_t balanceActionOffset = 140U + lateOffset;
  const size_t socOffset = 141U + lateOffset;
  const size_t remainingOffset = 142U + lateOffset;
  const size_t nominalOffset = 146U + lateOffset;
  const size_t cycleCountOffset = 150U + lateOffset;
  const size_t cycleCapacityOffset = 154U + lateOffset;
  const size_t sohOffset = 158U + lateOffset;
  const size_t runtimeOffset = 162U + lateOffset;
  const size_t chargeOffset = 166U + lateOffset;
  const size_t dischargeOffset = 167U + lateOffset;
  const size_t mosOffset = is32 ? 144U : 134U;

  data.valid = true;
  data.updatedAt = millis();

  data.cellCount = validCells;
  data.totalVoltage = static_cast<float>(readU32LE(frame, voltageOffset)) * 0.001f;
  data.current = static_cast<float>(readI32LE(frame, currentOffset)) * 0.001f;
  data.power = static_cast<int32_t>(lroundf(data.totalVoltage * data.current));
  data.soc = frame[socOffset];
  data.soh = frame[sohOffset];
  data.remainingCapacityAh =
      static_cast<float>(readU32LE(frame, remainingOffset)) * 0.001f;
  data.totalCapacityAh =
      static_cast<float>(readU32LE(frame, nominalOffset)) * 0.001f;
  data.cycleCapacityAh =
      static_cast<float>(readU32LE(frame, cycleCapacityOffset)) * 0.001f;
  data.reportedCycleCount = readU32LE(frame, cycleCountOffset);
  data.totalRuntimeSeconds = readU32LE(frame, runtimeOffset);
  data.chargeMos = frame[chargeOffset] != 0U;
  data.dischargeMos = frame[dischargeOffset] != 0U;
  data.balancerStatus = frame[balanceActionOffset] != 0U;
  const uint32_t rawErrors = is32
                                 ? readU32LE(frame, errorOffset)
                                 : static_cast<uint32_t>(readU16LE(frame, errorOffset));

  data.batteryStatus = static_cast<uint8_t>(rawErrors & 0xFFU);

  data.balanceMask = 0U;

  data.batteryType = is32 ? frame[243U + lateOffset] : 0U;
  data.mosTemperature = static_cast<int16_t>(lroundf(
      static_cast<float>(readI16LE(frame, mosOffset)) * 0.1f));

  const int16_t temperaturesRaw[5] = {
      readI16LE(frame, temp1Offset),
      readI16LE(frame, temp2Offset),
      readI16LE(frame, 222U + lateOffset),
      readI16LE(frame, 224U + lateOffset),
      readI16LE(frame, 226U + lateOffset),
  };
  for (uint8_t i = 0; i < 5U; ++i) {
    const int16_t value = static_cast<int16_t>(lroundf(
        static_cast<float>(temperaturesRaw[i]) * 0.1f));
    if (value >= AppConfig::Protocol::MinTemperatureC &&
        value <= AppConfig::Protocol::MaxTemperatureC) {
      data.temperatures[data.temperatureCount++] = value;
    }
  }

  if (validCells > 0U) {
    data.maxCellVoltage = maximum;
    data.minCellVoltage = minimum;
    data.deltaCellVoltage = maximum - minimum;
    data.averageCellVoltage = sum / static_cast<float>(validCells);
  }

  int score = 0;
  if (validCells >= 2U && validCells <= slotCount) score += 6;
  else score -= 12;
  score -= suspiciousCells * 2;
  if (enabledCells == 0U || enabledCells == validCells) score += 1;
  else if (enabledCells <= slotCount) score += 0;
  else score -= 2;

  if (finiteWithin(data.totalVoltage, 1.0f, AppConfig::Protocol::MaxPackVoltage)) score += 5;
  else score -= 10;
  if (finiteWithin(fabsf(data.current), 0.0f, AppConfig::Protocol::MaxAbsCurrent)) score += 2;
  else score -= 6;
  if (data.soc <= 100U) score += 3;
  else score -= 7;
  if (data.soh <= 100U) score += 1;
  if (finiteWithin(data.totalCapacityAh, 0.0f, AppConfig::Protocol::MaxCapacityAh)) score += 1;
  else score -= 3;
  if (finiteWithin(data.remainingCapacityAh, 0.0f, AppConfig::Protocol::MaxCapacityAh)) score += 1;
  else score -= 3;

  if (validCells > 0U && data.totalVoltage > 1.0f) {
    const float mismatch = fabsf(sum - data.totalVoltage);
    const float tolerance = fmaxf(2.0f, sum * 0.08f);
    if (mismatch <= tolerance) score += 8;
    else if (mismatch <= fmaxf(6.0f, sum * 0.25f)) score += 2;
    else score -= 8;
  }

  candidate.score = score;
  return candidate;
}

bool JkProtocolDecoder::parseStatusFrame(const uint8_t *frame,
                                         size_t length,
                                         BmsData &data) {
  if (frame == nullptr || length != kFrameSize || frame[4] != kStatusFrameType) {
    return false;
  }

  const Candidate candidate24 = parseStatusCandidate(frame, Variant::Jk02_24S);
  const Candidate candidate32 = parseStatusCandidate(frame, Variant::Jk02_32S);

  Candidate chosen;
  if (detectedVariant_ == Variant::Jk02_24S) {
    chosen = candidate32.score >= candidate24.score + 4
                 ? candidate32
                 : candidate24;
  } else if (detectedVariant_ == Variant::Jk02_32S) {
    chosen = candidate24.score >= candidate32.score + 4
                 ? candidate24
                 : candidate32;
  } else {
    chosen = candidate32.score > candidate24.score ? candidate32 : candidate24;
  }

  if (chosen.score >= 8 && chosen.variant != detectedVariant_) {
    const Variant previous = detectedVariant_;
    detectedVariant_ = chosen.variant;
    DiagnosticLog::printf(
        "JK protocol layout selected: %s (score=%d, previous=%s).\n",
        detectedVariant_ == Variant::Jk02_32S ? "JK02_32S" : "JK02_24S",
        chosen.score,
        previous == Variant::Jk02_32S ? "JK02_32S" :
        previous == Variant::Jk02_24S ? "JK02_24S" : "auto");
  }

  if (chosen.score < 2 || !chosen.data.valid || chosen.data.cellCount == 0U) {
    DiagnosticLog::printf("JK status frame rejected: layout score=%d.\n", chosen.score);
    return false;
  }

  data = chosen.data;
  return true;
}

bool JkProtocolDecoder::parseDeviceInfoFrame(const uint8_t *frame,
                                             size_t length,
                                             char *hardwareVersion,
                                             size_t hardwareSize,
                                             char *softwareVersion,
                                             size_t softwareSize) {
  if (frame == nullptr || length != kFrameSize ||
      frame[4] != kDeviceInfoFrameType ||
      !rangeValid(22U, 16U, length)) {
    return false;
  }

  copyProtocolText(hardwareVersion, hardwareSize, frame + 22U, 8U);
  copyProtocolText(softwareVersion, softwareSize, frame + 30U, 8U);

  const int major = hardwareMajor(hardwareVersion);
  if (major >= 11) detectedVariant_ = Variant::Jk02_32S;
  else if (major >= 6) detectedVariant_ = Variant::Jk02_24S;

  DiagnosticLog::printf("JK device info: HW=%s SW=%s layout=%s.\n",
                        hardwareVersion[0] == '\0' ? "unknown" : hardwareVersion,
                        softwareVersion[0] == '\0' ? "unknown" : softwareVersion,
                        detectedVariant_ == Variant::Jk02_32S ? "JK02_32S" :
                        detectedVariant_ == Variant::Jk02_24S ? "JK02_24S" : "auto");
  return hardwareVersion[0] != '\0' || softwareVersion[0] != '\0';
}
