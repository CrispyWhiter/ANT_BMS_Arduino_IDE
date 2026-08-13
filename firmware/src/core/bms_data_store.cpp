

#include "bms_data_store.h"

#include <stdio.h>
#include <string.h>

namespace BmsDataStore {
namespace {

BmsData sharedData;
portMUX_TYPE dataMux = portMUX_INITIALIZER_UNLOCKED;
volatile bool updatePending = false;

void copyVersion(char *destination, size_t destinationSize, const char *source) {
  if (destination == nullptr || destinationSize == 0U) return;
  if (source == nullptr) source = "";
  snprintf(destination, destinationSize, "%s", source);
}

}

void reset() {
  portENTER_CRITICAL(&dataMux);
  sharedData = BmsData{};
  updatePending = false;
  portEXIT_CRITICAL(&dataMux);
}

void invalidate() {
  portENTER_CRITICAL(&dataMux);
  sharedData = BmsData{};
  updatePending = true;
  portEXIT_CRITICAL(&dataMux);
}

void publishStatus(const BmsData &data) {
  portENTER_CRITICAL(&dataMux);

  char hardwareVersion[sizeof(sharedData.hardwareVersion)] = {};
  char softwareVersion[sizeof(sharedData.softwareVersion)] = {};
  memcpy(hardwareVersion, sharedData.hardwareVersion, sizeof(hardwareVersion));
  memcpy(softwareVersion, sharedData.softwareVersion, sizeof(softwareVersion));

  sharedData = data;
  memcpy(sharedData.hardwareVersion, hardwareVersion, sizeof(hardwareVersion));
  memcpy(sharedData.softwareVersion, softwareVersion, sizeof(softwareVersion));
  updatePending = true;

  portEXIT_CRITICAL(&dataMux);
}

void publishDeviceInfo(const char *hardwareVersion, const char *softwareVersion) {

  char hardwareCopy[sizeof(sharedData.hardwareVersion)] = {};
  char softwareCopy[sizeof(sharedData.softwareVersion)] = {};
  copyVersion(hardwareCopy, sizeof(hardwareCopy), hardwareVersion);
  copyVersion(softwareCopy, sizeof(softwareCopy), softwareVersion);

  portENTER_CRITICAL(&dataMux);
  memcpy(sharedData.hardwareVersion, hardwareCopy, sizeof(sharedData.hardwareVersion));
  memcpy(sharedData.softwareVersion, softwareCopy, sizeof(sharedData.softwareVersion));
  updatePending = true;
  portEXIT_CRITICAL(&dataMux);
}

bool consumePendingUpdate(BmsData &data) {
  bool hasUpdate = false;

  portENTER_CRITICAL(&dataMux);
  if (updatePending) {
    data = sharedData;
    updatePending = false;
    hasUpdate = true;
  }
  portEXIT_CRITICAL(&dataMux);

  return hasUpdate;
}

}
