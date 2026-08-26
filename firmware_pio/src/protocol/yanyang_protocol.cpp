#include "yanyang_protocol.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../core/diagnostic_log.h"
#include "../core/time_utils.h"

namespace {

constexpr uint8_t kReadHoldingRegisters = 0x03;
constexpr uint16_t kStartRegister = 75U;
constexpr uint16_t kRegisterCount = 92U;
constexpr size_t kExpectedDataBytes = kRegisterCount * 2U;
constexpr size_t kExpectedResponseBytes = 3U + kExpectedDataBytes + 2U;

size_t registerOffset(uint16_t registerAddress) {
  return static_cast<size_t>(registerAddress - kStartRegister) * 2U;
}

uint16_t readU16LE(const uint8_t *data, size_t offset) {
  return static_cast<uint16_t>(data[offset]) |
         (static_cast<uint16_t>(data[offset + 1U]) << 8U);
}

uint32_t readU32LE(const uint8_t *data, size_t offset) {
  return static_cast<uint32_t>(data[offset]) |
         (static_cast<uint32_t>(data[offset + 1U]) << 8U) |
         (static_cast<uint32_t>(data[offset + 2U]) << 16U) |
         (static_cast<uint32_t>(data[offset + 3U]) << 24U);
}

int32_t readI32LE(const uint8_t *data, size_t offset) {
  return static_cast<int32_t>(readU32LE(data, offset));
}

bool finiteWithin(float value, float minimum, float maximum) {
  return isfinite(value) && value >= minimum && value <= maximum;
}

}

YanyangProtocolDecoder::YanyangProtocolDecoder(StatusCallback statusCallback,
                                               DeviceInfoCallback deviceInfoCallback)
    : statusCallback_(statusCallback),
      deviceInfoCallback_(deviceInfoCallback) {}

void YanyangProtocolDecoder::reset() {
  frameLength_ = 0U;
  lastChunkAt_ = 0U;
  infoPublished_ = false;
}

uint16_t YanyangProtocolDecoder::crc16(const uint8_t *data, size_t length) {
  uint16_t crc = 0xFFFFU;
  if (data == nullptr) return crc;
  for (size_t i = 0U; i < length; ++i) {
    crc ^= data[i];
    for (uint8_t bit = 0U; bit < 8U; ++bit) {
      crc = (crc & 0x0001U) != 0U
                ? static_cast<uint16_t>((crc >> 1U) ^ 0xA001U)
                : static_cast<uint16_t>(crc >> 1U);
    }
  }
  return crc;
}

void YanyangProtocolDecoder::buildStatusRequest(uint8_t slaveAddress,
                                                uint8_t output[8]) {
  if (output == nullptr) return;
  output[0] = slaveAddress;
  output[1] = kReadHoldingRegisters;
  output[2] = static_cast<uint8_t>(kStartRegister >> 8U);
  output[3] = static_cast<uint8_t>(kStartRegister & 0xFFU);
  output[4] = static_cast<uint8_t>(kRegisterCount >> 8U);
  output[5] = static_cast<uint8_t>(kRegisterCount & 0xFFU);
  const uint16_t crc = crc16(output, 6U);
  output[6] = static_cast<uint8_t>(crc & 0xFFU);
  output[7] = static_cast<uint8_t>(crc >> 8U);
}

void YanyangProtocolDecoder::feed(const uint8_t *data, size_t length) {
  if (data == nullptr || length == 0U) return;

  const uint32_t now = millis();
  if (frameLength_ > 0U &&
      TimeUtils::elapsedSince(now, lastChunkAt_) >
          AppConfig::Protocol::FrameAssemblyTimeoutMs) {
    DiagnosticLog::write("Yanyang Modbus assembly timeout; buffer cleared.\n");
    frameLength_ = 0U;
  }
  lastChunkAt_ = now;

  if (length > sizeof(frameBuffer_)) {
    data += length - sizeof(frameBuffer_);
    length = sizeof(frameBuffer_);
    frameLength_ = 0U;
  }
  if (frameLength_ + length > sizeof(frameBuffer_)) {
    DiagnosticLog::write("Yanyang Modbus buffer overflow; resynchronizing.\n");
    frameLength_ = 0U;
  }

  memcpy(frameBuffer_ + frameLength_, data, length);
  frameLength_ += length;
  while (processBufferedFrames()) {}
}

bool YanyangProtocolDecoder::processBufferedFrames() {
  const uint8_t slave = AppConfig::Ble::YanyangDefaultModbusAddress;
  size_t start = 0U;
  while (start + 1U < frameLength_ &&
         !(frameBuffer_[start] == slave &&
           (frameBuffer_[start + 1U] == kReadHoldingRegisters ||
            frameBuffer_[start + 1U] == (kReadHoldingRegisters | 0x80U)))) {
    ++start;
  }
  if (start > 0U) {
    memmove(frameBuffer_, frameBuffer_ + start, frameLength_ - start);
    frameLength_ -= start;
  }
  if (frameLength_ < 3U) return false;

  const bool exception = (frameBuffer_[1] & 0x80U) != 0U;
  const size_t frameSize = exception ? 5U : static_cast<size_t>(frameBuffer_[2]) + 5U;
  if (frameSize > sizeof(frameBuffer_)) {
    memmove(frameBuffer_, frameBuffer_ + 1U, frameLength_ - 1U);
    --frameLength_;
    return true;
  }
  if (frameLength_ < frameSize) return false;

  const uint16_t storedCrc = static_cast<uint16_t>(frameBuffer_[frameSize - 2U]) |
                             (static_cast<uint16_t>(frameBuffer_[frameSize - 1U]) << 8U);
  const uint16_t calculatedCrc = crc16(frameBuffer_, frameSize - 2U);
  if (storedCrc == calculatedCrc) {
    if (exception) {
      DiagnosticLog::printf("Yanyang Modbus exception code=0x%02X.\n", frameBuffer_[2]);
    } else {
      parseStatusResponse(frameBuffer_, frameSize);
    }
  } else {
    DiagnosticLog::printf(
        "Yanyang Modbus CRC rejected: stored=0x%04X calculated=0x%04X.\n",
        static_cast<unsigned>(storedCrc),
        static_cast<unsigned>(calculatedCrc));
  }

  memmove(frameBuffer_, frameBuffer_ + frameSize, frameLength_ - frameSize);
  frameLength_ -= frameSize;
  return frameLength_ > 0U;
}

bool YanyangProtocolDecoder::parseStatusResponse(const uint8_t *frame,
                                                 size_t length) {
  if (frame == nullptr || length != kExpectedResponseBytes ||
      frame[0] != AppConfig::Ble::YanyangDefaultModbusAddress ||
      frame[1] != kReadHoldingRegisters || frame[2] != kExpectedDataBytes) {
    return false;
  }

  const uint8_t *dataBytes = frame + 3U;
  BmsData data;
  data.valid = true;
  data.updatedAt = millis();

  const size_t reg75 = registerOffset(75U);
  data.cellCount = min(dataBytes[reg75], AppConfig::Protocol::MaxCellCount);
  data.batteryType = dataBytes[reg75 + 1U];

  data.totalVoltage = readU32LE(dataBytes, registerOffset(76U)) * 0.001f;

  data.current = -readI32LE(dataBytes, registerOffset(78U)) * 0.01f;

  const uint8_t availableCells = min(data.cellCount, static_cast<uint8_t>(32U));
  for (uint8_t i = 0U; i < availableCells; ++i) {
    data.cells[i] = readU16LE(dataBytes, registerOffset(81U + i)) * 0.001f;
  }

  const size_t reg112 = registerOffset(112U);
  const size_t reg113 = registerOffset(113U);
  data.mosTemperature = static_cast<int16_t>(dataBytes[reg112 + 1U]) - 40;
  data.balancerTemperature = static_cast<int16_t>(dataBytes[reg112]) - 40;
  data.temperatureCount = 2U;
  data.temperatures[0] = static_cast<int16_t>(dataBytes[reg113 + 1U]) - 40;
  data.temperatures[1] = static_cast<int16_t>(dataBytes[reg113]) - 40;

  data.totalCapacityAh = readU16LE(dataBytes, registerOffset(118U)) * 0.1f;
  data.remainingCapacityAh = readU16LE(dataBytes, registerOffset(119U)) * 0.1f;
  const size_t reg120 = registerOffset(120U);
  data.soc = dataBytes[reg120] <= 100U ? dataBytes[reg120] : 100U;
  data.soh = dataBytes[reg120 + 1U] <= 100U ? dataBytes[reg120 + 1U] : 100U;

  data.balanceMask = readU32LE(dataBytes, registerOffset(139U));
  data.balancerStatus = data.balanceMask != 0U;
  const uint32_t runState = readU32LE(dataBytes, registerOffset(152U));
  data.dischargeMos = (runState & (1UL << 29U)) == 0U;
  data.chargeMos = (runState & (1UL << 28U)) == 0U;
  const uint32_t warningBits = readU32LE(dataBytes, registerOffset(156U));
  data.batteryStatus = static_cast<uint8_t>(warningBits & 0xFFU);
  data.power = static_cast<int32_t>(lroundf(data.totalVoltage * data.current));

  if (data.cellCount == 0U ||
      !finiteWithin(data.totalVoltage, 1.0f, AppConfig::Protocol::MaxPackVoltage) ||
      !finiteWithin(fabsf(data.current), 0.0f, AppConfig::Protocol::MaxAbsCurrent) ||
      !finiteWithin(data.totalCapacityAh, 0.0f, AppConfig::Protocol::MaxCapacityAh) ||
      !finiteWithin(data.remainingCapacityAh, 0.0f, AppConfig::Protocol::MaxCapacityAh)) {
    DiagnosticLog::write("Yanyang status rejected by field/range checks.\n");
    return false;
  }

  updateCellStatistics(data);
  if (!infoPublished_ && deviceInfoCallback_ != nullptr) {
    deviceInfoCallback_("YY Modbus", "RTU-BLE");
    infoPublished_ = true;
  }
  if (statusCallback_ != nullptr) statusCallback_(data);
  return true;
}

void YanyangProtocolDecoder::updateCellStatistics(BmsData &data) const {
  float minimum = 0.0f;
  float maximum = 0.0f;
  float sum = 0.0f;
  uint8_t validCount = 0U;
  for (uint8_t i = 0U; i < data.cellCount; ++i) {
    const float voltage = data.cells[i];
    if (!finiteWithin(voltage,
                      AppConfig::Protocol::MinCellVoltage,
                      AppConfig::Protocol::MaxCellVoltage)) {
      continue;
    }
    if (validCount == 0U || voltage < minimum) {
      minimum = voltage;
      data.minCell = i + 1U;
    }
    if (validCount == 0U || voltage > maximum) {
      maximum = voltage;
      data.maxCell = i + 1U;
    }
    sum += voltage;
    ++validCount;
  }
  data.minCellVoltage = validCount == 0U ? 0.0f : minimum;
  data.maxCellVoltage = validCount == 0U ? 0.0f : maximum;
  data.deltaCellVoltage = validCount == 0U ? 0.0f : maximum - minimum;
  data.averageCellVoltage = validCount == 0U ? 0.0f : sum / validCount;
}
