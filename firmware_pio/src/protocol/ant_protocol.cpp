#include "ant_protocol.h"

#include <math.h>
#include <string.h>

#include "../core/diagnostic_log.h"
#include "../core/time_utils.h"

namespace {

constexpr uint8_t kFrameStart0 = 0x7E;
constexpr uint8_t kFrameStart1 = 0xA1;
constexpr uint8_t kFrameEnd0 = 0xAA;
constexpr uint8_t kFrameEnd1 = 0x55;
constexpr uint8_t kStatusFrameType = 0x11;
constexpr uint8_t kDeviceInfoFrameType = 0x12;
constexpr size_t kMinimumFrameLength = 10;

namespace StatusOffset {
constexpr size_t TemperatureCount = 8;
constexpr size_t CellCount = 9;
constexpr size_t CellArray = 34;

constexpr size_t MosTemperature = 34;
constexpr size_t BalancerTemperature = 36;
constexpr size_t TotalVoltage = 38;
constexpr size_t Current = 40;
constexpr size_t Soc = 42;
constexpr size_t Soh = 44;
constexpr size_t ChargeMos = 46;
constexpr size_t DischargeMos = 47;
constexpr size_t BalancerStatus = 48;
constexpr size_t TotalCapacity = 50;
constexpr size_t RemainingCapacity = 54;
constexpr size_t CycleCapacity = 58;
constexpr size_t Power = 62;
constexpr size_t TotalRuntime = 66;
constexpr size_t BalanceMask = 70;
constexpr size_t MaxCellVoltage = 74;
constexpr size_t MaxCellIndex = 76;
constexpr size_t MinCellVoltage = 78;
constexpr size_t MinCellIndex = 80;
constexpr size_t DeltaCellVoltage = 82;
constexpr size_t AverageCellVoltage = 84;
constexpr size_t BatteryType = 94;
constexpr size_t TotalDischargeAh = 96;
constexpr size_t TotalChargeAh = 100;
constexpr size_t TotalDischargeSeconds = 104;
constexpr size_t TotalChargeSeconds = 108;
constexpr size_t KnownFieldsEnd = 112;
}

bool rangeValid(size_t index, size_t count, size_t length) {

  return index <= length && count <= (length - index);
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

uint16_t crc16Modbus(const uint8_t *data, size_t length) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < length; ++i) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8U; ++bit) {
      crc = (crc & 0x0001U)
                ? static_cast<uint16_t>((crc >> 1U) ^ 0xA001U)
                : static_cast<uint16_t>(crc >> 1U);
    }
  }
  return crc;
}

bool hasValidEnvelope(const uint8_t *frame, size_t length) {
  return frame != nullptr && length >= kMinimumFrameLength &&
         frame[0] == kFrameStart0 && frame[1] == kFrameStart1 &&
         frame[length - 2U] == kFrameEnd0 && frame[length - 1U] == kFrameEnd1;
}

bool hasValidDeclaredLength(const uint8_t *frame, size_t length) {
  if (frame == nullptr || length < 6U) return false;

  const size_t expectedLength = 6U + static_cast<size_t>(frame[5]) + 4U;
  return expectedLength == length;
}

bool hasValidCrc(const uint8_t *frame, size_t length) {
  if (frame == nullptr || length < 8U) return false;
  const uint16_t received = readU16LE(frame, length - 4U);
  const uint16_t calculated = crc16Modbus(frame + 1U, length - 5U);
  return received == calculated;
}

void copyProtocolText(char *destination,
                      size_t destinationSize,
                      const uint8_t *source,
                      size_t sourceLength) {
  if (destination == nullptr || destinationSize == 0U) return;

  const size_t copyLength = min(sourceLength, destinationSize - 1U);
  memcpy(destination, source, copyLength);
  destination[copyLength] = '\0';

  for (size_t i = copyLength; i > 0U; --i) {
    const char value = destination[i - 1U];
    if (value == '\0' || value == ' ' ||
        value == static_cast<char>(0xFF)) {
      destination[i - 1U] = '\0';
    } else {
      break;
    }
  }
}

bool finiteWithin(float value, float minimum, float maximum) {
  return isfinite(value) && value >= minimum && value <= maximum;
}

bool normalizeCompatibleStatus(BmsData &data) {
  if (data.cellCount == 0U ||
      data.cellCount > AppConfig::Protocol::MaxCellCount) {
    return false;
  }

  if (data.soc > 100U) {
    DiagnosticLog::printf("ANT SOC outside display range: %u; clamped to 100.\n",
                          data.soc);
    data.soc = 100U;
  }

  if (!finiteWithin(data.totalVoltage, 0.0f,
                    AppConfig::Protocol::MaxPackVoltage)) {
    DiagnosticLog::write("ANT pack voltage outside broad range; value cleared.\n");
    data.totalVoltage = 0.0f;
  }
  if (!finiteWithin(fabsf(data.current), 0.0f,
                    AppConfig::Protocol::MaxAbsCurrent)) {
    DiagnosticLog::write("ANT current outside broad range; value cleared.\n");
    data.current = 0.0f;
  }
  if (!finiteWithin(data.totalCapacityAh, 0.0f,
                    AppConfig::Protocol::MaxCapacityAh)) {
    data.totalCapacityAh = 0.0f;
  }
  if (!finiteWithin(data.remainingCapacityAh, 0.0f,
                    AppConfig::Protocol::MaxCapacityAh)) {
    data.remainingCapacityAh = 0.0f;
  }
  if (!finiteWithin(data.cycleCapacityAh, 0.0f,
                    AppConfig::Protocol::MaxCapacityAh)) {
    data.cycleCapacityAh = 0.0f;
  }

  for (uint8_t i = 0; i < data.temperatureCount; ++i) {
    if (data.temperatures[i] < AppConfig::Protocol::MinTemperatureC ||
        data.temperatures[i] > AppConfig::Protocol::MaxTemperatureC) {
      DiagnosticLog::printf("ANT temperature[%u]=%d C unavailable; value cleared.\n",
                            i,
                            data.temperatures[i]);
      data.temperatures[i] = 0;
    }
  }

  if (data.mosTemperature < AppConfig::Protocol::MinTemperatureC ||
      data.mosTemperature > AppConfig::Protocol::MaxTemperatureC) {
    data.mosTemperature =
        data.temperatureCount > 0U ? data.temperatures[0] : 0;
  }
  if (data.balancerTemperature < AppConfig::Protocol::MinTemperatureC ||
      data.balancerTemperature > AppConfig::Protocol::MaxTemperatureC) {
    data.balancerTemperature = 0;
  }

  float cellSum = 0.0f;
  float derivedMaximum = -1.0f;
  float derivedMinimum = AppConfig::Protocol::MaxCellVoltage + 1.0f;
  uint16_t derivedMaximumIndex = 0;
  uint16_t derivedMinimumIndex = 0;
  uint8_t validCellCount = 0;

  for (uint8_t i = 0; i < data.cellCount; ++i) {
    const float voltage = data.cells[i];

    if (!finiteWithin(voltage,
                      AppConfig::Protocol::MinCellVoltage,
                      AppConfig::Protocol::MaxCellVoltage)) {
      continue;
    }

    ++validCellCount;
    cellSum += voltage;
    if (voltage > derivedMaximum) {
      derivedMaximum = voltage;
      derivedMaximumIndex = i + 1U;
    }
    if (voltage < derivedMinimum) {
      derivedMinimum = voltage;
      derivedMinimumIndex = i + 1U;
    }
  }

  if (validCellCount > 0U) {
    data.maxCellVoltage = derivedMaximum;
    data.maxCell = derivedMaximumIndex;
    data.minCellVoltage = derivedMinimum;
    data.minCell = derivedMinimumIndex;
    data.deltaCellVoltage = derivedMaximum - derivedMinimum;
    data.averageCellVoltage = cellSum / static_cast<float>(validCellCount);

    if (data.totalVoltage > 1.0f) {
      const float mismatch = fabsf(data.totalVoltage - cellSum);
      const float tolerance = fmaxf(10.0f, cellSum * 0.35f);
      if (mismatch > tolerance) {
        DiagnosticLog::printf(
            "ANT pack/cell values differ across firmware layout: pack=%.2f sum=%.2f; frame retained.\n",
            data.totalVoltage,
            cellSum);
      }
    }
  }

  if (fabsf(static_cast<float>(data.power)) >
          AppConfig::Protocol::MaxAbsPowerW ||
      (data.power == 0 && fabsf(data.current) >= 0.2f)) {
    data.power = static_cast<int32_t>(
        lroundf(data.totalVoltage * data.current));
  }
  return true;
}

}

AntProtocolDecoder::AntProtocolDecoder(StatusCallback statusCallback,
                                       DeviceInfoCallback deviceInfoCallback)
    : statusCallback_(statusCallback),
      deviceInfoCallback_(deviceInfoCallback) {}

void AntProtocolDecoder::reset() {
  frameLength_ = 0;
  lastChunkAt_ = 0;
}

void AntProtocolDecoder::feed(const uint8_t *data, size_t length) {
  if (data == nullptr || length == 0U) return;

  const uint32_t now = millis();
  if (frameLength_ > 0U &&
      TimeUtils::elapsedSince(now, lastChunkAt_) >
          AppConfig::Protocol::FrameAssemblyTimeoutMs) {
    DiagnosticLog::write("ANT frame assembly timeout; buffer cleared.\n");
    reset();
  }
  lastChunkAt_ = now;

  if (length > sizeof(frameBuffer_) - frameLength_) {
    DiagnosticLog::write("ANT frame buffer overflow; buffer cleared.\n");
    reset();
    if (length > sizeof(frameBuffer_)) return;
  }

  memcpy(frameBuffer_ + frameLength_, data, length);
  frameLength_ += length;
  processBufferedFrames();
}

bool AntProtocolDecoder::processBufferedFrames() {
  bool processedAny = false;

  while (frameLength_ > 0U) {

    size_t headerIndex = 0U;
    while (headerIndex + 1U < frameLength_ &&
           !(frameBuffer_[headerIndex] == kFrameStart0 &&
             frameBuffer_[headerIndex + 1U] == kFrameStart1)) {
      ++headerIndex;
    }

    if (headerIndex > 0U) {
      const size_t remaining = frameLength_ - headerIndex;
      memmove(frameBuffer_, frameBuffer_ + headerIndex, remaining);
      frameLength_ = remaining;
    }

    if (frameLength_ < 2U) return processedAny;
    if (frameBuffer_[0] != kFrameStart0 ||
        frameBuffer_[1] != kFrameStart1) {
      frameLength_ = 0U;
      return processedAny;
    }
    if (frameLength_ < 6U) return processedAny;

    const size_t expectedLength =
        6U + static_cast<size_t>(frameBuffer_[5]) + 4U;
    if (expectedLength < kMinimumFrameLength ||
        expectedLength > sizeof(frameBuffer_)) {
      DiagnosticLog::printf("Invalid ANT declared frame length: %u\n",
                            static_cast<unsigned>(expectedLength));
      memmove(frameBuffer_, frameBuffer_ + 1U, frameLength_ - 1U);
      --frameLength_;
      continue;
    }

    if (frameLength_ < expectedLength) return processedAny;

    if (frameBuffer_[expectedLength - 2U] != kFrameEnd0 ||
        frameBuffer_[expectedLength - 1U] != kFrameEnd1) {
      DiagnosticLog::write("Invalid ANT frame trailer; resynchronizing.\n");
      memmove(frameBuffer_, frameBuffer_ + 1U, frameLength_ - 1U);
      --frameLength_;
      continue;
    }

    processFrame(frameBuffer_, expectedLength);
    processedAny = true;

    const size_t remaining = frameLength_ - expectedLength;
    if (remaining > 0U) {
      memmove(frameBuffer_, frameBuffer_ + expectedLength, remaining);
    }
    frameLength_ = remaining;
  }

  return processedAny;
}

void AntProtocolDecoder::processFrame(const uint8_t *frame, size_t length) {
  if (frame == nullptr || length < 3U) return;

  switch (frame[2]) {
    case kStatusFrameType: {
      BmsData parsed;
      if (parseStatusFrame(frame, length, parsed) &&
          statusCallback_ != nullptr) {
        statusCallback_(parsed);
      }
      return;
    }

    case kDeviceInfoFrameType: {
      char hardwareVersion[17] = {};
      char softwareVersion[17] = {};
      if (parseDeviceInfoFrame(frame,
                               length,
                               hardwareVersion,
                               sizeof(hardwareVersion),
                               softwareVersion,
                               sizeof(softwareVersion)) &&
          deviceInfoCallback_ != nullptr) {
        deviceInfoCallback_(hardwareVersion, softwareVersion);
      }
      return;
    }

    default:
      DiagnosticLog::printf("Unhandled ANT frame type 0x%02X length=%u\n",
                            frame[2],
                            static_cast<unsigned>(length));
      return;
  }
}

bool AntProtocolDecoder::parseStatusFrame(const uint8_t *frame,
                                          size_t length,
                                          BmsData &parsed) {
  if (!hasValidEnvelope(frame, length) ||
      !hasValidDeclaredLength(frame, length)) {
    return false;
  }
  if (!hasValidCrc(frame, length)) {
    DiagnosticLog::write("Status frame CRC error.\n");
    return false;
  }

  if (!rangeValid(StatusOffset::TemperatureCount, 2U, length)) return false;
  const uint8_t temperatureCount = frame[StatusOffset::TemperatureCount];
  const uint8_t cellCount = frame[StatusOffset::CellCount];

  if (temperatureCount > AppConfig::Protocol::MaxTemperatureCount ||
      cellCount == 0U ||
      cellCount > AppConfig::Protocol::MaxCellCount) {
    DiagnosticLog::printf("Invalid sensor counts: temperatures=%u cells=%u\n",
                          temperatureCount,
                          cellCount);
    return false;
  }

  const size_t cellBytes = static_cast<size_t>(cellCount) * 2U;
  const size_t temperatureBytes = static_cast<size_t>(temperatureCount) * 2U;
  const size_t temperatureStart = StatusOffset::CellArray + cellBytes;
  const size_t dynamicOffset = cellBytes + temperatureBytes;
  const size_t finalKnownFieldEnd = StatusOffset::KnownFieldsEnd + dynamicOffset;

  if (!rangeValid(StatusOffset::CellArray, cellBytes, length) ||
      !rangeValid(temperatureStart, temperatureBytes, length) ||
      finalKnownFieldEnd > length - 4U) {
    DiagnosticLog::write("Status frame field bounds invalid.\n");
    return false;
  }

  parsed = BmsData{};
  parsed.valid = true;
  parsed.updatedAt = millis();
  parsed.permissions = frame[6];
  parsed.batteryStatus = frame[7];
  parsed.temperatureCount = temperatureCount;
  parsed.cellCount = cellCount;

  for (uint8_t i = 0; i < cellCount; ++i) {
    parsed.cells[i] =
        readU16LE(frame,
                  StatusOffset::CellArray + static_cast<size_t>(i) * 2U) *
        0.001f;
  }

  for (uint8_t i = 0; i < AppConfig::Protocol::MaxTemperatureCount; ++i) {
    parsed.temperatures[i] = -40;
  }
  for (uint8_t i = 0; i < temperatureCount; ++i) {
    parsed.temperatures[i] = readI16LE(
        frame, temperatureStart + static_cast<size_t>(i) * 2U);
  }

  parsed.mosTemperature =
      readI16LE(frame, StatusOffset::MosTemperature + dynamicOffset);
  parsed.balancerTemperature =
      readI16LE(frame, StatusOffset::BalancerTemperature + dynamicOffset);
  parsed.totalVoltage =
      readU16LE(frame, StatusOffset::TotalVoltage + dynamicOffset) * 0.01f;

  parsed.current =
      -readI16LE(frame, StatusOffset::Current + dynamicOffset) * 0.1f;
  parsed.soc = readU16LE(frame, StatusOffset::Soc + dynamicOffset);
  parsed.soh = readU16LE(frame, StatusOffset::Soh + dynamicOffset);
  parsed.chargeMos = frame[StatusOffset::ChargeMos + dynamicOffset];
  parsed.dischargeMos = frame[StatusOffset::DischargeMos + dynamicOffset];
  parsed.balancerStatus = frame[StatusOffset::BalancerStatus + dynamicOffset];

  parsed.totalCapacityAh =
      readU32LE(frame, StatusOffset::TotalCapacity + dynamicOffset) * 0.000001f;
  parsed.remainingCapacityAh =
      readU32LE(frame, StatusOffset::RemainingCapacity + dynamicOffset) *
      0.000001f;
  parsed.cycleCapacityAh =
      readU32LE(frame, StatusOffset::CycleCapacity + dynamicOffset) * 0.001f;

  parsed.power = static_cast<int32_t>(
      lroundf(parsed.totalVoltage * parsed.current));
  parsed.totalRuntimeSeconds =
      readU32LE(frame, StatusOffset::TotalRuntime + dynamicOffset);
  parsed.balanceMask =
      readU32LE(frame, StatusOffset::BalanceMask + dynamicOffset);

  parsed.maxCellVoltage =
      readU16LE(frame, StatusOffset::MaxCellVoltage + dynamicOffset) * 0.001f;
  parsed.maxCell =
      readU16LE(frame, StatusOffset::MaxCellIndex + dynamicOffset);
  parsed.minCellVoltage =
      readU16LE(frame, StatusOffset::MinCellVoltage + dynamicOffset) * 0.001f;
  parsed.minCell =
      readU16LE(frame, StatusOffset::MinCellIndex + dynamicOffset);
  parsed.deltaCellVoltage =
      readU16LE(frame, StatusOffset::DeltaCellVoltage + dynamicOffset) * 0.001f;
  parsed.averageCellVoltage =
      readU16LE(frame, StatusOffset::AverageCellVoltage + dynamicOffset) *
      0.001f;

  parsed.batteryType =
      readU16LE(frame, StatusOffset::BatteryType + dynamicOffset);
  parsed.totalDischargeAh =
      readU32LE(frame, StatusOffset::TotalDischargeAh + dynamicOffset) * 0.001f;
  parsed.totalChargeAh =
      readU32LE(frame, StatusOffset::TotalChargeAh + dynamicOffset) * 0.001f;
  parsed.totalDischargeSeconds =
      readU32LE(frame, StatusOffset::TotalDischargeSeconds + dynamicOffset);
  parsed.totalChargeSeconds =
      readU32LE(frame, StatusOffset::TotalChargeSeconds + dynamicOffset);

  if (!normalizeCompatibleStatus(parsed)) return false;

  const uint32_t now = parsed.updatedAt;
  if (lastStatusLogAt_ == 0U ||
      TimeUtils::elapsedSince(now, lastStatusLogAt_) >=
          AppConfig::Protocol::StatusLogPeriodMs) {
    lastStatusLogAt_ = now;
    DiagnosticLog::printf(
        "[%lu ms] DATA %.2fV %.1fA %u%% %.1f/%.1fAh cells=%u delta=%umV\n",
        static_cast<unsigned long>(now),
        parsed.totalVoltage,
        parsed.current,
        parsed.soc,
        parsed.remainingCapacityAh,
        parsed.totalCapacityAh,
        parsed.cellCount,
        static_cast<unsigned>(
            lroundf(parsed.deltaCellVoltage * 1000.0f)));
  }
  return true;
}

bool AntProtocolDecoder::parseDeviceInfoFrame(
    const uint8_t *frame,
    size_t length,
    char *hardwareVersion,
    size_t hardwareSize,
    char *softwareVersion,
    size_t softwareSize) const {
  if (!hasValidEnvelope(frame, length) ||
      !hasValidDeclaredLength(frame, length)) {
    return false;
  }

  if (!rangeValid(6U, 32U, length - 4U)) {
    DiagnosticLog::write("Device info frame field bounds invalid.\n");
    return false;
  }

  if (!hasValidCrc(frame, length)) {
    DiagnosticLog::write(
        "Device info frame CRC warning; payload accepted for compatibility.\n");
  }

  copyProtocolText(hardwareVersion, hardwareSize, frame + 6U, 16U);
  copyProtocolText(softwareVersion, softwareSize, frame + 22U, 16U);
  DiagnosticLog::printf("Device info: hardware=%s software=%s\n",
                        hardwareVersion,
                        softwareVersion);
  return true;
}
