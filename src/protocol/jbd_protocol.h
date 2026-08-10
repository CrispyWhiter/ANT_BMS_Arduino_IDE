#pragma once

#include <Arduino.h>

#include "../bms_model.h"

class JbdProtocolDecoder {
 public:
  using StatusCallback = void (*)(const BmsData &data);
  using DeviceInfoCallback = void (*)(const char *hardwareVersion,
                                      const char *softwareVersion);

  JbdProtocolDecoder(StatusCallback statusCallback,
                     DeviceInfoCallback deviceInfoCallback);

  void reset();

  void feed(const uint8_t *data, size_t length);

  static void buildReadCommand(uint8_t command, uint8_t output[7]);

 private:
  bool processBufferedFrames();
  void processFrame(const uint8_t *frame, size_t length);
  bool parseBasicStatus(const uint8_t *payload, size_t length);
  bool parseCellVoltages(const uint8_t *payload, size_t length);
  void publishStatus();
  void updateCellStatistics();

  const StatusCallback statusCallback_;
  const DeviceInfoCallback deviceInfoCallback_;
  uint8_t frameBuffer_[AppConfig::Protocol::MaxFrameSize] = {};
  size_t frameLength_ = 0;
  uint32_t lastChunkAt_ = 0;
  BmsData latest_;
  bool haveBasic_ = false;
};
