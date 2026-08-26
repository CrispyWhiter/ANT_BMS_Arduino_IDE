#pragma once

#include <Arduino.h>

#include "../bms_model.h"

class AntProtocolDecoder {
 public:
  using StatusCallback = void (*)(const BmsData &data);
  using DeviceInfoCallback = void (*)(const char *hardwareVersion,
                                      const char *softwareVersion);

  AntProtocolDecoder(StatusCallback statusCallback,
                     DeviceInfoCallback deviceInfoCallback);

  void reset();

  void feed(const uint8_t *data, size_t length);

 private:
  bool processBufferedFrames();
  void processFrame(const uint8_t *frame, size_t length);
  bool parseStatusFrame(const uint8_t *frame, size_t length, BmsData &parsedData);
  bool parseDeviceInfoFrame(const uint8_t *frame,
                            size_t length,
                            char *hardwareVersion,
                            size_t hardwareSize,
                            char *softwareVersion,
                            size_t softwareSize) const;

  const StatusCallback statusCallback_;
  const DeviceInfoCallback deviceInfoCallback_;
  uint8_t frameBuffer_[AppConfig::Protocol::MaxFrameSize] = {};
  size_t frameLength_ = 0;
  uint32_t lastChunkAt_ = 0;
  uint32_t lastStatusLogAt_ = 0;
};
