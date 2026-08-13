#pragma once

#include <Arduino.h>

#include "../bms_type.h"

namespace BmsBleService {

struct ScanDevice {
  char name[48] = {};
  char address[24] = {};
  int rssi = -127;
};

bool begin();
void loop();
void setWebPortalActive(bool active);
bool isConnected();
bool hasValidStatus();

void requestConfigurationScan(BmsType type);
void cancelConfigurationScan();

bool saveConfiguredTargetForRestart(BmsType type,
                                    const char *address,
                                    const char *name);

bool hasConfiguredDevice();
bool clearPairedDevice();
size_t copyScanResults(ScanDevice *destination, size_t capacity);
bool isConfigurationScanComplete();
bool prepareForWebPortal();

const char *pairedDeviceAddress();
BmsType pairedBmsType();

BmsType configurationScanType();

}
