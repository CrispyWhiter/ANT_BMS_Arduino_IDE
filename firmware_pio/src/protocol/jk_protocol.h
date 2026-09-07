#pragma once

#include <Arduino.h>

#include "../bms_model.h"

class JkProtocolDecoder {
 public:
  using StatusCallback = void (*)(const BmsData &data);
  using DeviceInfoCallback = void (*)(const char *hardwareVersion,
                                      const char *softwareVersion);

  enum class Variant : uint8_t {
    Auto = 0,
    Jk02_24S,
    Jk02_32S,
    Jk04,
  };

  JkProtocolDecoder(StatusCallback statusCallback,
                    DeviceInfoCallback deviceInfoCallback);

  void reset();

  void feed(const uint8_t *data, size_t length);

  Variant detectedVariant() const;

  static void buildReadCommand(uint8_t command, uint8_t output[20]);

 private:
  struct Candidate {
    BmsData data;
    int score = -1000;
    Variant variant = Variant::Auto;
  };

  bool processBufferedFrames();
  void processFrame(const uint8_t *frame, size_t length);
  bool parseStatusFrame(const uint8_t *frame, size_t length, BmsData &data);
  Candidate parseStatusCandidate(const uint8_t *frame, Variant variant) const;
  Candidate parseStatusJk04(const uint8_t *frame, Variant variant) const;
  bool parseDeviceInfoFrame(const uint8_t *frame,
                            size_t length,
                            char *hardwareVersion,
                            size_t hardwareSize,
                            char *softwareVersion,
                            size_t softwareSize);

  const StatusCallback statusCallback_;
  const DeviceInfoCallback deviceInfoCallback_;
  uint8_t frameBuffer_[AppConfig::Protocol::MaxFrameSize] = {};
  size_t frameLength_ = 0;
  uint32_t lastChunkAt_ = 0;
  Variant detectedVariant_ = Variant::Auto;
};
