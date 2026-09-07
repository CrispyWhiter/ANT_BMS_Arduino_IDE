#include "bms_ble_service.h"

#include <NimBLEDevice.h>
#include <strings.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <sdkconfig.h>

#include "../app_config.h"
#include "../core/runtime_profile.h"
#include "../core/bms_data_store.h"
#include "../core/diagnostic_log.h"
#include "../core/time_utils.h"
#include "../display_ui.h"
#include "../protocol/ant_protocol.h"
#include "../protocol/jk_protocol.h"
#include "../protocol/jbd_protocol.h"
#include "../protocol/yanyang_protocol.h"
#include "../storage/paired_device_store.h"

namespace BmsBleService {
namespace {

const uint8_t kStatusRequest[] = {
    0x7E, 0xA1, 0x01, 0x00, 0x00, 0xBE, 0x18, 0x55, 0xAA, 0x55};
const uint8_t kDeviceInfoRequest[] = {
    0x7E, 0xA1, 0x02, 0x6C, 0x02, 0x20, 0x58, 0xC4, 0xAA, 0x55};

enum class PendingAction : uint8_t {
  None,
  StartPairingScan,
  RecoverLink,
  PauseForWeb,
};

enum class ScanMode : uint8_t {
  None,
  Reconnect,
  Pairing,
};

enum class LinkState : uint8_t {
  Idle,
  Scanning,
  ConnectScheduled,
  Connecting,
  Online,
  Disconnecting,
  FastReconnectWait,
};

NimBLEClient *client = nullptr;
NimBLERemoteCharacteristic *writeCharacteristic = nullptr;
NimBLERemoteCharacteristic *notifyCharacteristic = nullptr;

bool initialized = false;
bool radioInitialized = false;
bool connected = false;
bool webPortalActive = false;
bool webPortalStateKnown = false;
uint32_t webPortalResumeAt = 0;
LinkState linkState = LinkState::Idle;
PendingAction pendingAction = PendingAction::None;
bool waitingForActionDisconnect = false;
uint32_t actionDisconnectDeadlineAt = 0;

char targetName[48] = {};
char targetAddress[24] = {};
NimBLEAddress targetPeerAddress;
bool targetPeerAddressValid = false;
int connectionRssi = -127;

char pairedName[48] = {};
char pairedAddress[24] = {};
BmsType pairedType = BmsType::Ant;
BmsType activeConnectionType = BmsType::Ant;
BmsType configurationType = BmsType::Ant;
bool configurationScanComplete = true;

ScanDevice scanDevices[AppConfig::Ble::MaxConfigurationResults] = {};
size_t scanDeviceCount = 0;
portMUX_TYPE scanResultsMux = portMUX_INITIALIZER_UNLOCKED;

portMUX_TYPE eventMux = portMUX_INITIALIZER_UNLOCKED;
ScanMode callbackScanMode = ScanMode::None;
bool callbackScanActive = false;
bool suppressNextScanEnd = false;
uint32_t suppressScanEndUntil = 0;

bool disconnectEventPending = false;
int disconnectEventReason = 0;
uint32_t disconnectEventAt = 0;

bool scanEndEventPending = false;
ScanMode scanEndEventMode = ScanMode::None;
int scanEndEventReason = 0;

bool targetEventPending = false;
NimBLEAddress targetEventPeerAddress;
char targetEventName[48] = {};
char targetEventAddress[24] = {};
int targetEventRssi = -127;

char desiredAddressForScan[24] = {};
char desiredNameForScan[48] = {};

uint8_t notificationBuffer[AppConfig::Ble::NotificationBufferSize] = {};
size_t notificationHead = 0;
size_t notificationTail = 0;
size_t notificationCount = 0;
bool notificationOverflow = false;
portMUX_TYPE notificationMux = portMUX_INITIALIZER_UNLOCKED;

uint32_t connectAt = 0;
uint32_t reconnectAt = 0;
uint32_t fastReconnectAt = 0;
uint8_t fastReconnectAttempts = 0;

uint32_t nextDeviceInfoAt = 0;
uint32_t nextStatusAt = 0;
uint32_t nextAuxRequestAt = 0;
uint32_t firstStatusDeadlineAt = 0;
uint32_t lastStatusReceivedAt = 0;
volatile uint32_t lastNotificationReceivedAt = 0;
uint32_t nextStreamProbeAt = 0;
bool firstStatusReceived = false;
uint8_t firstStatusRequestCount = 0;
uint8_t consecutiveWriteFailures = 0;
uint8_t streamRecoveryAttempts = 0;

uint32_t connectionReadyAt = 0;
uint32_t lastDisconnectAt = 0;
bool logFirstStatusLatency = false;

const char *disconnectReasonText(int reason);

bool automaticBleAllowed() {

  return !webPortalActive;
}

bool hasUsefulDesiredName() {
  return desiredNameForScan[0] != '\0' &&
         strcasecmp(desiredNameForScan, "ANT BMS") != 0 &&
         strcasecmp(desiredNameForScan, "JK BMS") != 0;
}

bool addressesEqual(const char *left, const char *right) {
  return left != nullptr && right != nullptr && left[0] != '\0' && right[0] != '\0' &&
         strcasecmp(left, right) == 0;
}

bool namesEqual(const char *left, const char *right) {
  return left != nullptr && right != nullptr && left[0] != '\0' && right[0] != '\0' &&
         strcasecmp(left, right) == 0;
}

bool hasStoredTarget() {
  return pairedAddress[0] != '\0' || pairedName[0] != '\0';
}

bool isValidMacAddress(const char *address) {
  if (address == nullptr || strlen(address) != 17U) return false;
  for (size_t i = 0; i < 17U; ++i) {
    if (i == 2U || i == 5U || i == 8U || i == 11U || i == 14U) {
      if (address[i] != ':') return false;
    } else if (!isxdigit(static_cast<unsigned char>(address[i]))) {
      return false;
    }
  }
  return true;
}

bool containsCaseInsensitive(const std::string &text, const char *needle) {
  if (needle == nullptr || needle[0] == '\0') return true;
  const size_t needleLength = strlen(needle);
  if (text.size() < needleLength) return false;

  for (size_t start = 0; start + needleLength <= text.size(); ++start) {
    bool matches = true;
    for (size_t i = 0; i < needleLength; ++i) {
      const unsigned char left = static_cast<unsigned char>(text[start + i]);
      const unsigned char right = static_cast<unsigned char>(needle[i]);
      if (tolower(left) != tolower(right)) {
        matches = false;
        break;
      }
    }
    if (matches) return true;
  }
  return false;
}

bool isAntDeviceName(const std::string &name) {
  if (name.empty()) return false;
  if (containsCaseInsensitive(name, AppConfig::Ble::AntPairingNameKeyword)) return true;
  return name.size() >= 3U &&
         tolower(static_cast<unsigned char>(name[0])) == 'a' &&
         tolower(static_cast<unsigned char>(name[1])) == 'n' &&
         tolower(static_cast<unsigned char>(name[2])) == 't';
}

bool isJikongDeviceName(const std::string &name) {
  if (name.size() < 2U) return false;
  return tolower(static_cast<unsigned char>(name[0])) == 'j' &&
         tolower(static_cast<unsigned char>(name[1])) == 'k';
}

bool isJiabaidaDeviceName(const std::string &name) {
  return containsCaseInsensitive(name, AppConfig::Ble::JiabaidaNameKeyword) ||
         containsCaseInsensitive(name, AppConfig::Ble::JiabaidaAlternateNameKeyword);
}

bool isYanyangDeviceName(const std::string &name) {
  return containsCaseInsensitive(name, "Yanyang") ||
         containsCaseInsensitive(name, "YYBMS") ||
         containsCaseInsensitive(name, "YY-BMS") ||
         containsCaseInsensitive(name, "HLK-B40") ||
         containsCaseInsensitive(name, AppConfig::Ble::YanyangModuleKeyword);
}

bool isDeviceNameForType(const std::string &name, BmsType type) {
  switch (type) {
    case BmsType::Jikong: return isJikongDeviceName(name);
    case BmsType::Jiabaida: return isJiabaidaDeviceName(name);
    case BmsType::Yanyang: return isYanyangDeviceName(name);
    case BmsType::Ant:
    default: return isAntDeviceName(name);
  }
}

bool advertisesService(const NimBLEAdvertisedDevice *device, const char *uuid) {
  return device != nullptr && uuid != nullptr &&
         device->isAdvertisingService(NimBLEUUID(uuid));
}

bool hasJikongMacOui(const std::string &address) {
  // 极空 BLE 模块的 MAC OUI 前缀（参考 esphome-jk-bms 抓包注释）：
  //   老 BLE 模块：C8:47:8C  新 BLE 模块：20:21:11
  if (address.size() < 8U) return false;
  std::string prefix;
  prefix.reserve(8U);
  for (size_t i = 0; i < 8U; ++i) {
    prefix.push_back(static_cast<char>(tolower(static_cast<unsigned char>(address[i]))));
  }
  return prefix == "c8:47:8c" || prefix == "20:21:11";
}

bool advertisesJikongService(const NimBLEAdvertisedDevice *device) {
  // 极空 BLE 模块广播 0xFFE0 服务（与连接阶段端点查找的 CommonServiceUuid 一致）
  return advertisesService(device, AppConfig::Ble::CommonServiceUuid);
}

bool advertisedDeviceMatchesType(const NimBLEAdvertisedDevice *device,
                                  BmsType type) {
  if (device == nullptr) return false;
  const std::string name = device->haveName() ? device->getName() : std::string();
  if (isDeviceNameForType(name, type)) return true;

  if (type == BmsType::Jikong) {
    // 移植自 esphome-jk-bms：除名称外，
    // ① 广播含 0xFFE0 服务（极空 BLE 模块标准服务）即视为极空；
    // ② 极空模块固定 MAC OUI（C8:47:8C / 20:21:11）作为辅助判定。
    if (advertisesJikongService(device)) return true;
    return hasJikongMacOui(device->getAddress().toString());
  }
  if (type == BmsType::Jiabaida) {
    return advertisesService(device, AppConfig::Ble::JiabaidaServiceUuid);
  }
  if (type == BmsType::Yanyang) {
    return advertisesService(device, AppConfig::Ble::CommonServiceUuid) ||
           advertisesService(device, AppConfig::Ble::JiabaidaServiceUuid) ||
           advertisesService(device, AppConfig::Ble::NordicUartServiceUuid);
  }
  return false;
}

bool typeRequiresPeriodicPolling(BmsType type) {
  return type != BmsType::Jikong;
}

void clearNotificationQueue() {
  portENTER_CRITICAL(&notificationMux);
  notificationHead = 0;
  notificationTail = 0;
  notificationCount = 0;
  notificationOverflow = false;
  portEXIT_CRITICAL(&notificationMux);
}

void enqueueNotification(const uint8_t *data, size_t length) {
  if (data == nullptr || length == 0U) return;

  lastNotificationReceivedAt = millis();
  portENTER_CRITICAL(&notificationMux);

  if (length > sizeof(notificationBuffer)) {
    notificationHead = 0;
    notificationTail = 0;
    notificationCount = 0;
    notificationOverflow = true;
    portEXIT_CRITICAL(&notificationMux);
    return;
  }

  const size_t freeSpace = sizeof(notificationBuffer) - notificationCount;
  if (length > freeSpace) {

    notificationHead = 0;
    notificationTail = 0;
    notificationCount = 0;
    notificationOverflow = true;
  }

  const size_t firstPart = min(length, sizeof(notificationBuffer) - notificationHead);
  memcpy(notificationBuffer + notificationHead, data, firstPart);
  if (length > firstPart) {
    memcpy(notificationBuffer, data + firstPart, length - firstPart);
  }
  notificationHead = (notificationHead + length) % sizeof(notificationBuffer);
  notificationCount += length;

  portEXIT_CRITICAL(&notificationMux);
}

size_t popNotificationBytes(uint8_t *destination, size_t capacity) {
  if (destination == nullptr || capacity == 0U) return 0U;

  portENTER_CRITICAL(&notificationMux);
  const size_t count = min(capacity, notificationCount);
  const size_t firstPart = min(count, sizeof(notificationBuffer) - notificationTail);
  if (firstPart > 0U) memcpy(destination, notificationBuffer + notificationTail, firstPart);
  if (count > firstPart) memcpy(destination + firstPart, notificationBuffer, count - firstPart);
  notificationTail = (notificationTail + count) % sizeof(notificationBuffer);
  notificationCount -= count;
  portEXIT_CRITICAL(&notificationMux);
  return count;
}

bool consumeNotificationOverflow() {
  portENTER_CRITICAL(&notificationMux);
  const bool overflow = notificationOverflow;
  notificationOverflow = false;
  portEXIT_CRITICAL(&notificationMux);
  return overflow;
}

void clearScanResults() {
  portENTER_CRITICAL(&scanResultsMux);
  for (size_t i = 0; i < AppConfig::Ble::MaxConfigurationResults; ++i) {
    scanDevices[i] = ScanDevice{};
  }
  scanDeviceCount = 0;
  portEXIT_CRITICAL(&scanResultsMux);
}

void recordScanResult(const char *name, const char *address, int rssiValue) {
  if (name == nullptr || address == nullptr) return;

  portENTER_CRITICAL(&scanResultsMux);
  size_t index = scanDeviceCount;
  for (size_t i = 0; i < scanDeviceCount; ++i) {
    if (addressesEqual(scanDevices[i].address, address)) {
      index = i;
      break;
    }
  }

  if (index == scanDeviceCount) {
    if (scanDeviceCount >= AppConfig::Ble::MaxConfigurationResults) {

      index = 0;
      for (size_t i = 1; i < scanDeviceCount; ++i) {
        if (scanDevices[i].rssi < scanDevices[index].rssi) index = i;
      }
      if (rssiValue <= scanDevices[index].rssi) {
        portEXIT_CRITICAL(&scanResultsMux);
        return;
      }
    } else {
      ++scanDeviceCount;
    }
  }

  snprintf(scanDevices[index].name, sizeof(scanDevices[index].name), "%s", name);
  snprintf(scanDevices[index].address, sizeof(scanDevices[index].address), "%s", address);
  scanDevices[index].rssi = rssiValue;
  portEXIT_CRITICAL(&scanResultsMux);
}

void setCallbackScanState(ScanMode mode, bool active) {
  portENTER_CRITICAL(&eventMux);
  callbackScanMode = mode;
  callbackScanActive = active;
  portEXIT_CRITICAL(&eventMux);
}

ScanMode getCallbackScanMode() {
  portENTER_CRITICAL(&eventMux);
  const ScanMode mode = callbackScanMode;
  portEXIT_CRITICAL(&eventMux);
  return mode;
}

bool getCallbackScanActive() {
  portENTER_CRITICAL(&eventMux);
  const bool active = callbackScanActive;
  portEXIT_CRITICAL(&eventMux);
  return active;
}

void suppressIntentionalScanEnd() {
  portENTER_CRITICAL(&eventMux);
  suppressNextScanEnd = true;
  suppressScanEndUntil = TimeUtils::deadlineAfter(
      millis(), AppConfig::Ble::ScanEndSuppressWindowMs);
  callbackScanMode = ScanMode::None;
  callbackScanActive = false;
  portEXIT_CRITICAL(&eventMux);
}

void clearPendingCallbackEvents() {
  portENTER_CRITICAL(&eventMux);
  targetEventPending = false;
  scanEndEventPending = false;
  portEXIT_CRITICAL(&eventMux);
}

void postTargetEvent(const NimBLEAdvertisedDevice *advertisedDevice,
                     const char *observedName) {
  if (advertisedDevice == nullptr) return;

  const std::string address = advertisedDevice->getAddress().toString();
  const char *resolvedName = observedName;
  if (resolvedName == nullptr || resolvedName[0] == '\0') resolvedName = desiredNameForScan;
  if (resolvedName == nullptr || resolvedName[0] == '\0') resolvedName = BmsTypeInfo::defaultDeviceName(activeConnectionType);

  bool shouldStop = false;
  portENTER_CRITICAL(&eventMux);
  if (!targetEventPending && callbackScanActive) {
    targetEventPeerAddress = advertisedDevice->getAddress();
    snprintf(targetEventName, sizeof(targetEventName), "%s", resolvedName);
    snprintf(targetEventAddress, sizeof(targetEventAddress), "%s", address.c_str());
    targetEventRssi = advertisedDevice->getRSSI();
    targetEventPending = true;
    suppressNextScanEnd = true;
    suppressScanEndUntil = TimeUtils::deadlineAfter(
        millis(), AppConfig::Ble::ScanEndSuppressWindowMs);
    callbackScanMode = ScanMode::None;
    callbackScanActive = false;
    shouldStop = true;
  }
  portEXIT_CRITICAL(&eventMux);

  if (shouldStop) {
    NimBLEScan *scan = NimBLEDevice::getScan();
    if (scan != nullptr) scan->stop();
  }
}

bool takeTargetEvent() {
  bool pending = false;
  portENTER_CRITICAL(&eventMux);
  if (targetEventPending) {
    targetPeerAddress = targetEventPeerAddress;
    snprintf(targetName, sizeof(targetName), "%s", targetEventName);
    snprintf(targetAddress, sizeof(targetAddress), "%s", targetEventAddress);
    connectionRssi = targetEventRssi;
    targetEventPending = false;
    pending = true;
  }
  portEXIT_CRITICAL(&eventMux);
  return pending;
}

bool takeDisconnectEvent(int &reason, uint32_t &eventAt) {
  bool pending = false;
  portENTER_CRITICAL(&eventMux);
  if (disconnectEventPending) {
    reason = disconnectEventReason;
    eventAt = disconnectEventAt;
    disconnectEventPending = false;
    pending = true;
  }
  portEXIT_CRITICAL(&eventMux);
  return pending;
}

bool takeScanEndEvent(ScanMode &mode, int &reason) {
  bool pending = false;
  portENTER_CRITICAL(&eventMux);
  if (scanEndEventPending) {
    mode = scanEndEventMode;
    reason = scanEndEventReason;
    scanEndEventPending = false;
    pending = true;
  }
  portEXIT_CRITICAL(&eventMux);
  return pending;
}

void onStatusData(const BmsData &data) {
  if (!connected) return;

  const uint32_t now = millis();
  const bool wasWaitingForFirstFrame = !firstStatusReceived;

  firstStatusReceived = true;
  lastStatusReceivedAt = now;
  nextStatusAt = typeRequiresPeriodicPolling(activeConnectionType)
                     ? TimeUtils::deadlineAfter(now, AppConfig::Ble::StatusRequestPeriodMs)
                     : 0U;
  nextStreamProbeAt = TimeUtils::deadlineAfter(now, AppConfig::Ble::StatusProbeAfterMs);
  consecutiveWriteFailures = 0;
  streamRecoveryAttempts = 0;
  if (nextDeviceInfoAt == 0U) {
    nextDeviceInfoAt = TimeUtils::deadlineAfter(now, AppConfig::Ble::DeviceInfoAfterFirstStatusMs);
  }

  if (wasWaitingForFirstFrame && logFirstStatusLatency) {
    logFirstStatusLatency = false;
    DiagnosticLog::printf(
        "[%lu ms] First live BMS frame: %lu ms after link ready, %lu ms after disconnect.\n",
        static_cast<unsigned long>(now),
        static_cast<unsigned long>(TimeUtils::elapsedSince(now, connectionReadyAt)),
        static_cast<unsigned long>(
            lastDisconnectAt == 0U
                ? 0U
                : TimeUtils::elapsedSince(now, lastDisconnectAt)));
  }

  if (targetAddress[0] != '\0') {

    const bool addressChanged = !addressesEqual(pairedAddress, targetAddress);
    const bool nameChanged =
        targetName[0] != '\0' && !namesEqual(pairedName, targetName);
    if (addressChanged || nameChanged) {
      const char *verifiedName = targetName[0] == '\0' ? pairedName : targetName;
      if (PairedDeviceStore::save(activeConnectionType, targetAddress, verifiedName)) {
        snprintf(pairedAddress, sizeof(pairedAddress), "%s", targetAddress);
        snprintf(pairedName, sizeof(pairedName), "%s", verifiedName);
        DiagnosticLog::printf("Verified BMS target committed: %s (%s).\n",
                              pairedName,
                              pairedAddress);
      } else {
        DiagnosticLog::write("Unable to persist verified BMS target.\n");
      }
    }
  }

  BmsDataStore::publishStatus(data);
}

void onDeviceInfo(const char *hardwareVersion, const char *softwareVersion) {
  if (!connected) return;
  BmsDataStore::publishDeviceInfo(hardwareVersion, softwareVersion);
}

AntProtocolDecoder antProtocolDecoder(onStatusData, onDeviceInfo);
JkProtocolDecoder jkProtocolDecoder(onStatusData, onDeviceInfo);
JbdProtocolDecoder jbdProtocolDecoder(onStatusData, onDeviceInfo);
YanyangProtocolDecoder yanyangProtocolDecoder(onStatusData, onDeviceInfo);

void resetAllProtocolDecoders() {
  antProtocolDecoder.reset();
  jkProtocolDecoder.reset();
  jbdProtocolDecoder.reset();
  yanyangProtocolDecoder.reset();
}

void notifyCallback(NimBLERemoteCharacteristic *characteristic,
                    uint8_t *data,
                    size_t length,
                    bool isNotify) {
  (void)characteristic;
  (void)isNotify;
  enqueueNotification(data, length);
}

class ClientCallbacks : public NimBLEClientCallbacks {
 public:
  void onConnect(NimBLEClient *connectedClient) override {
    if (connectedClient == nullptr) return;
    DiagnosticLog::printf("[%lu ms] BLE link connected: %s, MTU=%u.\n",
                          static_cast<unsigned long>(millis()),
                          connectedClient->getPeerAddress().toString().c_str(),
                          static_cast<unsigned>(connectedClient->getMTU()));
  }

  void onDisconnect(NimBLEClient *disconnectedClient, int reason) override {
    if (disconnectedClient != client) return;

    portENTER_CRITICAL(&eventMux);
    disconnectEventReason = reason;
    disconnectEventAt = millis();
    disconnectEventPending = true;
    portEXIT_CRITICAL(&eventMux);
  }
};

ClientCallbacks clientCallbacks;

class ScanCallbacks : public NimBLEScanCallbacks {
 public:

  void recordConfigurationDevice(const NimBLEAdvertisedDevice *advertisedDevice) {
    if (advertisedDevice == nullptr ||
        !advertisedDeviceMatchesType(advertisedDevice, configurationType)) {
      return;
    }

    const std::string advertisedName =
        advertisedDevice->haveName()
            ? advertisedDevice->getName()
            : std::string(BmsTypeInfo::defaultDeviceName(configurationType));
    const std::string address = advertisedDevice->getAddress().toString();
    recordScanResult(advertisedName.c_str(),
                     address.c_str(),
                     advertisedDevice->getRSSI());
  }

  void onResult(const NimBLEAdvertisedDevice *advertisedDevice) override {
    if (advertisedDevice == nullptr) return;

    const ScanMode mode = getCallbackScanMode();
    if (mode == ScanMode::None) return;

    const std::string address = advertisedDevice->getAddress().toString();
    const bool hasName = advertisedDevice->haveName();
    const std::string name = hasName ? advertisedDevice->getName() : std::string();

    if (mode == ScanMode::Pairing) {

      recordConfigurationDevice(advertisedDevice);
      return;
    }

    const bool addressMatch =
        desiredAddressForScan[0] != '\0' &&
        addressesEqual(address.c_str(), desiredAddressForScan);
    const bool nameMatch =
        hasUsefulDesiredName() && hasName &&
        namesEqual(name.c_str(), desiredNameForScan);

    if (addressMatch || nameMatch) {
      postTargetEvent(advertisedDevice,
                      hasName ? name.c_str() : desiredNameForScan);
    }
  }

  void onScanEnd(const NimBLEScanResults &results, int reason) override {
    (void)results;
    const uint32_t now = millis();

    portENTER_CRITICAL(&eventMux);
    if (suppressNextScanEnd && !TimeUtils::deadlineReached(now, suppressScanEndUntil)) {
      suppressNextScanEnd = false;
      suppressScanEndUntil = 0;
      portEXIT_CRITICAL(&eventMux);
      return;
    }
    suppressNextScanEnd = false;
    suppressScanEndUntil = 0;

    const ScanMode endedMode = callbackScanMode;
    callbackScanMode = ScanMode::None;
    callbackScanActive = false;
    if (endedMode != ScanMode::None) {
      scanEndEventMode = endedMode;
      scanEndEventReason = reason;
      scanEndEventPending = true;
    }
    portEXIT_CRITICAL(&eventMux);
  }
};

ScanCallbacks scanCallbacks;

void processNotificationQueue() {
  if (consumeNotificationOverflow()) {
    resetAllProtocolDecoders();
    DiagnosticLog::write("BLE notification ring overflow; protocol assembly resynchronized.\n");
  }

  uint8_t chunk[AppConfig::Ble::NotificationDrainChunkSize];
  for (;;) {
    const size_t count = popNotificationBytes(chunk, sizeof(chunk));
    if (count == 0U) break;
    if (!connected) continue;
    switch (activeConnectionType) {
      case BmsType::Jikong: jkProtocolDecoder.feed(chunk, count); break;
      case BmsType::Jiabaida: jbdProtocolDecoder.feed(chunk, count); break;
      case BmsType::Yanyang: yanyangProtocolDecoder.feed(chunk, count); break;
      case BmsType::Ant:
      default: antProtocolDecoder.feed(chunk, count); break;
    }
  }
}

void clearConnectionState(bool preserveGattCache) {
  connected = false;
  connectionRssi = -127;
  if (!preserveGattCache) {
    writeCharacteristic = nullptr;
    notifyCharacteristic = nullptr;
  }

  nextDeviceInfoAt = 0;
  nextStatusAt = 0;
  nextAuxRequestAt = 0;
  firstStatusDeadlineAt = 0;
  lastStatusReceivedAt = 0;
  lastNotificationReceivedAt = 0;
  nextStreamProbeAt = 0;
  firstStatusReceived = false;
  firstStatusRequestCount = 0;
  consecutiveWriteFailures = 0;
  streamRecoveryAttempts = 0;
  logFirstStatusLatency = false;
  resetAllProtocolDecoders();
  clearNotificationQueue();

  DisplayUi::setConnected(false);
  BmsDataStore::invalidate();
}

void deleteClient() {
  if (client == nullptr) return;
  NimBLEClient *doomed = client;
  client = nullptr;
  writeCharacteristic = nullptr;
  notifyCharacteristic = nullptr;
  NimBLEDevice::deleteClient(doomed);
}

bool hasReconnectTarget() {
  return pairedAddress[0] != '\0' || pairedName[0] != '\0';
}

void scheduleReconnectScan(uint32_t delayMs) {
  fastReconnectAt = 0;
  linkState = LinkState::Idle;

  if (!automaticBleAllowed()) {
    reconnectAt = 0;
    return;
  }

  if (!hasReconnectTarget()) {
    reconnectAt = 0;
    return;
  }

  const uint32_t now = millis();
  const uint32_t requestedAt = TimeUtils::deadlineAfter(now, delayMs);
  reconnectAt = webPortalResumeAt != 0U &&
                        static_cast<int32_t>(webPortalResumeAt - requestedAt) > 0
                    ? webPortalResumeAt
                    : requestedAt;
}

bool sendRequest(const uint8_t *data, size_t length, const char *label) {
  if (!connected || client == nullptr || !client->isConnected() ||
      writeCharacteristic == nullptr) {
    if (consecutiveWriteFailures < 0xFFU) ++consecutiveWriteFailures;
    return false;
  }

  const bool responseRequired = !writeCharacteristic->canWriteNoResponse();
  const bool written = writeCharacteristic->writeValue(
      data, length, responseRequired);
  if (!written) {
    if (consecutiveWriteFailures < 0xFFU) ++consecutiveWriteFailures;
    DiagnosticLog::printf("[%lu ms] %s failed (consecutive=%u, nimble=%d).\n",
                          static_cast<unsigned long>(millis()),
                          label,
                          static_cast<unsigned>(consecutiveWriteFailures),
                          client->getLastError());
  } else {
    consecutiveWriteFailures = 0;
  }
  return written;
}

bool sendJkCommand(uint8_t command, const char *label) {
  uint8_t request[20] = {};
  JkProtocolDecoder::buildReadCommand(command, request);
  return sendRequest(request, sizeof(request), label);
}

bool sendJbdCommand(uint8_t command, const char *label) {
  uint8_t request[7] = {};
  JbdProtocolDecoder::buildReadCommand(command, request);
  return sendRequest(request, sizeof(request), label);
}

bool sendYanyangStatusRequest() {
  uint8_t request[8] = {};
  YanyangProtocolDecoder::buildStatusRequest(
      AppConfig::Ble::YanyangDefaultModbusAddress, request);
  return sendRequest(request, sizeof(request), "Yanyang Modbus status request");
}

bool sendStatusRequestForActiveType() {
  switch (activeConnectionType) {
    case BmsType::Jikong:
      return sendJkCommand(0x96, "JK status stream command");
    case BmsType::Jiabaida: {
      const bool sent = sendJbdCommand(0x03, "JBD basic status request");
      if (sent) {
        nextAuxRequestAt = TimeUtils::deadlineAfter(
            millis(), AppConfig::Ble::JiabaidaCellRequestDelayMs);
      }
      return sent;
    }
    case BmsType::Yanyang:
      return sendYanyangStatusRequest();
    case BmsType::Ant:
    default:
      return sendRequest(kStatusRequest, sizeof(kStatusRequest), "ANT status request");
  }
}

bool sendDeviceInfoRequestForActiveType() {
  switch (activeConnectionType) {
    case BmsType::Jikong:
      return sendJkCommand(0x97, "JK device info request");
    case BmsType::Jiabaida:
      return sendJbdCommand(0x05, "JBD hardware info request");
    case BmsType::Yanyang:

      return true;
    case BmsType::Ant:
    default:
      return sendRequest(kDeviceInfoRequest,
                         sizeof(kDeviceInfoRequest),
                         "ANT device info request");
  }
}

void sendInitialStatusRequest(uint32_t now) {
  ++firstStatusRequestCount;
  sendStatusRequestForActiveType();

  const uint32_t retryDelay =
      firstStatusRequestCount < AppConfig::Ble::FirstStatusFastRequestCount
          ? AppConfig::Ble::FirstStatusFastRetryMs
          : AppConfig::Ble::FirstStatusSlowRetryMs;
  nextStatusAt = TimeUtils::deadlineAfter(now, retryDelay);
}

void beginStatusAcquisition() {
  const uint32_t now = millis();
  connectionReadyAt = now;
  firstStatusReceived = false;
  lastStatusReceivedAt = 0;
  firstStatusRequestCount = 0;
  firstStatusDeadlineAt = TimeUtils::deadlineAfter(now, AppConfig::Ble::FirstStatusTimeoutMs);
  nextDeviceInfoAt = 0;
  nextAuxRequestAt = 0;
  nextStreamProbeAt = TimeUtils::deadlineAfter(now, AppConfig::Ble::StatusProbeAfterMs);
  lastNotificationReceivedAt = 0;
  consecutiveWriteFailures = 0;
  streamRecoveryAttempts = 0;
  logFirstStatusLatency = true;

  if (activeConnectionType == BmsType::Jikong) {

    sendDeviceInfoRequestForActiveType();
    nextStatusAt = TimeUtils::deadlineAfter(
        now, AppConfig::Ble::FirstStatusFastRetryMs);
  } else {
    sendInitialStatusRequest(now);
  }
}

bool findEndpointsInService(const char *serviceUuid,
                            const char *exactWriteUuid,
                            const char *exactNotifyUuid,
                            bool allowAnyCharacteristic,
                            bool useCachedAttributes) {
  if (client == nullptr || serviceUuid == nullptr) return false;
  NimBLERemoteService *service = client->getService(serviceUuid);
  if (service == nullptr) return false;

  NimBLERemoteCharacteristic *candidateWrite = nullptr;
  NimBLERemoteCharacteristic *candidateNotify = nullptr;
  const NimBLEUUID writeUuid(exactWriteUuid == nullptr ? "0000" : exactWriteUuid);
  const NimBLEUUID notifyUuid(exactNotifyUuid == nullptr ? "0000" : exactNotifyUuid);
  const auto &characteristics = service->getCharacteristics(!useCachedAttributes);
  for (NimBLERemoteCharacteristic *characteristic : characteristics) {
    if (characteristic == nullptr) continue;
    const bool writeUuidMatches = allowAnyCharacteristic ||
        (exactWriteUuid != nullptr && characteristic->getUUID().equals(writeUuid));
    const bool notifyUuidMatches = allowAnyCharacteristic ||
        (exactNotifyUuid != nullptr && characteristic->getUUID().equals(notifyUuid));

    if (candidateWrite == nullptr && writeUuidMatches &&
        (characteristic->canWriteNoResponse() || characteristic->canWrite())) {
      candidateWrite = characteristic;
    }
    if (candidateNotify == nullptr && notifyUuidMatches &&
        (characteristic->canNotify() || characteristic->canIndicate())) {
      candidateNotify = characteristic;
    }
  }

  if (candidateWrite == nullptr || candidateNotify == nullptr) return false;
  writeCharacteristic = candidateWrite;
  notifyCharacteristic = candidateNotify;
  return true;
}

bool discoverAndSubscribe(bool useCachedAttributes) {
  if (!useCachedAttributes) {
    writeCharacteristic = nullptr;
    notifyCharacteristic = nullptr;
  }

  if (writeCharacteristic == nullptr || notifyCharacteristic == nullptr) {
    bool found = false;
    switch (activeConnectionType) {
      case BmsType::Jiabaida:
        found = findEndpointsInService(
            AppConfig::Ble::JiabaidaServiceUuid,
            AppConfig::Ble::JiabaidaWriteUuid,
            AppConfig::Ble::JiabaidaNotifyUuid,
            false,
            useCachedAttributes);
        break;
      case BmsType::Yanyang:
        found = findEndpointsInService(
            AppConfig::Ble::CommonServiceUuid,
            nullptr,
            nullptr,
            true,
            useCachedAttributes);
        if (!found) {
          found = findEndpointsInService(
              AppConfig::Ble::JiabaidaServiceUuid,
              nullptr,
              nullptr,
              true,
              useCachedAttributes);
        }
        if (!found) {
          found = findEndpointsInService(
              AppConfig::Ble::NordicUartServiceUuid,
              AppConfig::Ble::NordicUartWriteUuid,
              AppConfig::Ble::NordicUartNotifyUuid,
              false,
              useCachedAttributes);
        }
        break;
      case BmsType::Jikong:
      case BmsType::Ant:
      default:
        found = findEndpointsInService(
            AppConfig::Ble::CommonServiceUuid,
            AppConfig::Ble::CommonDataUuid,
            AppConfig::Ble::CommonDataUuid,
            false,
            useCachedAttributes);
        break;
    }

    if (!found) {
      char message[80] = {};
      snprintf(message,
               sizeof(message),
               "%s GATT write/notify endpoint not found",
               BmsTypeInfo::key(activeConnectionType));
      DiagnosticLog::printf("%s.\n", message);
      return false;
    }
  }

  const bool useNotify = notifyCharacteristic->canNotify();
  if (!notifyCharacteristic->subscribe(useNotify, notifyCallback, true)) {
    char message[72] = {};
    snprintf(message,
             sizeof(message),
             "%s notification subscription failed",
             BmsTypeInfo::key(activeConnectionType));
    DiagnosticLog::printf("%s.\n", message);
    return false;
  }

  DiagnosticLog::printf(
      "%s endpoints ready: write=0x%04X notify=0x%04X.\n",
      BmsTypeInfo::key(activeConnectionType),
      static_cast<unsigned>(writeCharacteristic->getHandle()),
      static_cast<unsigned>(notifyCharacteristic->getHandle()));
  return true;
}

bool finishConnectedSession(bool useCachedAttributes) {
  if (client == nullptr || !client->isConnected()) return false;
  if (!discoverAndSubscribe(useCachedAttributes)) return false;

  connected = true;
  linkState = LinkState::Online;
  reconnectAt = 0;
  fastReconnectAt = 0;
  fastReconnectAttempts = 0;
  resetAllProtocolDecoders();
  clearNotificationQueue();
  connectionRssi = client->getRssi();
  DisplayUi::setConnected(true);

  DiagnosticLog::printf("[%lu ms] BLE ready. Type=%s RSSI=%d dBm, GATT=%s.\n",
                        static_cast<unsigned long>(millis()),
                        BmsTypeInfo::key(activeConnectionType),
                        connectionRssi,
                        useCachedAttributes ? "cached" : "discovered");
  beginStatusAcquisition();
  return true;
}

void stopScanIfRunning() {
  if (!getCallbackScanActive()) return;
  suppressIntentionalScanEnd();
  NimBLEScan *scan = NimBLEDevice::getScan();
  if (scan != nullptr) scan->stop();
}

void prepareDesiredScanTarget() {
  activeConnectionType = pairedType;
  snprintf(desiredAddressForScan, sizeof(desiredAddressForScan), "%s", pairedAddress);
  snprintf(desiredNameForScan, sizeof(desiredNameForScan), "%s", pairedName);
}

bool ensureRadioInitialized() {
  if (radioInitialized) return true;
  DiagnosticLog::write("Starting NimBLE radio on demand.\n");
  if (!NimBLEDevice::init(AppConfig::Ble::LocalDeviceName)) {
    DiagnosticLog::write("NimBLE initialization failed.\n");
    return false;
  }
  radioInitialized = true;
  return true;
}

void startScan(bool pairingOnly) {
  if (!pairingOnly && !automaticBleAllowed()) {
    linkState = LinkState::Idle;
    reconnectAt = 0;
    return;
  }

  if (!ensureRadioInitialized()) {
    linkState = LinkState::Idle;
    if (pairingOnly) configurationScanComplete = true;
    return;
  }

  clearPendingCallbackEvents();
  targetPeerAddressValid = false;
  targetAddress[0] = '\0';
  targetName[0] = '\0';
  connectAt = 0;
  reconnectAt = 0;
  fastReconnectAt = 0;
  clearConnectionState(false);

  if (pairingOnly) clearScanResults();
  prepareDesiredScanTarget();

  if (!pairingOnly && desiredAddressForScan[0] == '\0' &&
      desiredNameForScan[0] == '\0') {
    linkState = LinkState::Idle;
    DiagnosticLog::write("No configured BMS target; reconnect scan remains idle.\n");
    return;
  }

  NimBLEScan *scan = NimBLEDevice::getScan();
  if (scan == nullptr) {
    DiagnosticLog::write("BLE scan object unavailable.\n");
    if (pairingOnly) configurationScanComplete = true;
    else scheduleReconnectScan(AppConfig::Ble::ScanRetryDelayMs);
    return;
  }

  scan->setScanCallbacks(&scanCallbacks, false);
  const bool needsScanResponse = pairingOnly || hasUsefulDesiredName();
  scan->setActiveScan(needsScanResponse);
  scan->setInterval(needsScanResponse
                        ? AppConfig::Ble::ConfigurationScanInterval
                        : AppConfig::Ble::ReconnectScanInterval);
  scan->setWindow(needsScanResponse
                      ? AppConfig::Ble::ConfigurationScanWindow
                      : AppConfig::Ble::ReconnectScanWindow);

  scan->setMaxResults(0);

  const bool nameFallbackReconnect = !pairingOnly && hasUsefulDesiredName();
  const uint32_t duration = (pairingOnly || nameFallbackReconnect)
                                ? AppConfig::Ble::ConfigurationScanDurationMs
                                : AppConfig::Ble::ReconnectScanDurationMs;
  const ScanMode mode = pairingOnly ? ScanMode::Pairing : ScanMode::Reconnect;
  setCallbackScanState(mode, true);
  linkState = LinkState::Scanning;

  DiagnosticLog::printf("[%lu ms] Starting %s BLE scan: type=%s, duration=%lu ms.\n",
                        static_cast<unsigned long>(millis()),
                        pairingOnly ? "Web configuration" : "reconnect",
                        BmsTypeInfo::key(pairingOnly ? configurationType : pairedType),
                        static_cast<unsigned long>(duration));
  if (!scan->start(duration, false, true)) {
    setCallbackScanState(ScanMode::None, false);
    linkState = LinkState::Idle;
    DiagnosticLog::write("Unable to start BLE scan.\n");
    if (pairingOnly) configurationScanComplete = true;
    else scheduleReconnectScan(AppConfig::Ble::ScanRetryDelayMs);
  }
}

bool connectFreshTarget() {
  if (!ensureRadioInitialized()) return false;
  if (!targetPeerAddressValid || targetAddress[0] == '\0') return false;

  deleteClient();
  client = NimBLEDevice::createClient();
  if (client == nullptr) {
    DiagnosticLog::write("Unable to create BLE client.\n");
    return false;
  }

  client->setClientCallbacks(&clientCallbacks, false);
  client->setConnectionParams(AppConfig::Ble::ConnectionMinInterval,
                              AppConfig::Ble::ConnectionMaxInterval,
                              AppConfig::Ble::ConnectionLatency,
                              AppConfig::Ble::ConnectionSupervisionTimeout);
  client->setConnectTimeout(AppConfig::Ble::ConnectTimeoutMs);
  client->setConnectRetries(AppConfig::Ble::ConnectRetries);
  linkState = LinkState::Connecting;

  DiagnosticLog::printf("[%lu ms] Fresh connection to %s...\n",
                        static_cast<unsigned long>(millis()),
                        targetAddress);

  if (!client->connect(targetPeerAddress, true, false, true)) {
    DiagnosticLog::write("Fresh connection failed.\n");
    deleteClient();
    return false;
  }

  if (!finishConnectedSession(false)) {
    deleteClient();
    return false;
  }
  return true;
}

bool connectCachedClient() {
  if (!ensureRadioInitialized()) return false;
  if (client == nullptr || client->isConnected()) return false;

  client->setConnectionParams(AppConfig::Ble::ConnectionMinInterval,
                              AppConfig::Ble::ConnectionMaxInterval,
                              AppConfig::Ble::ConnectionLatency,
                              AppConfig::Ble::ConnectionSupervisionTimeout);
  client->setConnectTimeout(AppConfig::Ble::FastReconnectTimeoutMs);
  client->setConnectRetries(0);
  linkState = LinkState::Connecting;

  DiagnosticLog::printf("[%lu ms] Fast reconnect using cached peer/GATT...\n",
                        static_cast<unsigned long>(millis()));

  if (!client->connect(false, false, false)) {
    DiagnosticLog::write("Cached reconnect failed.\n");
    return false;
  }

  if (!finishConnectedSession(true)) return false;
  return true;
}

void scheduleFastReconnect() {
  fastReconnectAttempts = 0;
  if (!automaticBleAllowed()) {
    fastReconnectAt = 0;
    linkState = LinkState::Idle;
    return;
  }
  fastReconnectAt = TimeUtils::deadlineAfter(
      millis(), AppConfig::Ble::FastReconnectDelayMs + RuntimeProfile::linkBiasMs());
  linkState = LinkState::FastReconnectWait;
}

void finishActionWithoutConnectedClient(PendingAction action, bool cacheAvailable) {
  pendingAction = PendingAction::None;
  waitingForActionDisconnect = false;
  actionDisconnectDeadlineAt = 0;

  if (action == PendingAction::StartPairingScan) {
    clearConnectionState(false);
    deleteClient();
    startScan(true);
    return;
  }

  if (action == PendingAction::PauseForWeb) {
    clearConnectionState(false);
    deleteClient();
    linkState = LinkState::Idle;

    scheduleReconnectScan(AppConfig::WebConfig::BleResumeAfterClientLeavesMs);
    return;
  }

  if (action == PendingAction::RecoverLink) {

    clearConnectionState(cacheAvailable);
    if (cacheAvailable && client != nullptr) {
      scheduleFastReconnect();
    } else {
      deleteClient();
      scheduleReconnectScan(AppConfig::Ble::ScanRetryDelayMs);
    }
  }
}

void performAction(PendingAction action) {
  if (action == PendingAction::None || waitingForActionDisconnect) return;

  stopScanIfRunning();
  clearPendingCallbackEvents();
  reconnectAt = 0;
  fastReconnectAt = 0;
  connectAt = 0;
  targetPeerAddressValid = false;

  if (client != nullptr && client->isConnected()) {
    waitingForActionDisconnect = true;
    actionDisconnectDeadlineAt = TimeUtils::deadlineAfter(millis(), AppConfig::Ble::ActionDisconnectTimeoutMs);
    linkState = LinkState::Disconnecting;
    if (!client->disconnect()) {
      DiagnosticLog::write("Intentional BLE disconnect request failed; forcing cleanup.\n");
      NimBLEClient *doomed = client;
      client = nullptr;
      writeCharacteristic = nullptr;
      notifyCharacteristic = nullptr;
      NimBLEDevice::deleteClient(doomed);
      finishActionWithoutConnectedClient(action, false);
    }
    return;
  }

  finishActionWithoutConnectedClient(action, false);
}

void handleDisconnectEvent(int reason, uint32_t eventAt) {
  lastDisconnectAt = eventAt;
  DiagnosticLog::printf("[%lu ms] BLE disconnected, reason=%d (0x%02X, %s).\n",
                        static_cast<unsigned long>(eventAt),
                        reason,
                        static_cast<unsigned>(reason & 0xFF),
                        disconnectReasonText(reason));

  if (waitingForActionDisconnect) {
    const PendingAction action = pendingAction;
    const bool preserveCache = action == PendingAction::RecoverLink;
    finishActionWithoutConnectedClient(action, preserveCache);
    return;
  }

  const uint32_t sessionAge = connectionReadyAt == 0U
                                  ? 0U
                                  : eventAt - connectionReadyAt;
  const bool peerTerminated = (reason & 0xFF) == 0x13;
  const bool rapidDisconnect = sessionAge > 0U &&
      sessionAge < AppConfig::Ble::RapidDisconnectWindowMs;

  if (peerTerminated || rapidDisconnect) {

    clearConnectionState(false);
    deleteClient();
    scheduleReconnectScan(AppConfig::Ble::PeerDisconnectRetryDelayMs);
  } else {
    clearConnectionState(true);
    scheduleFastReconnect();
  }
}

void handleActionDisconnectTimeout() {
  if (!waitingForActionDisconnect) return;

  const PendingAction action = pendingAction;
  DiagnosticLog::write("Intentional BLE disconnect timed out; forcing client deletion.\n");
  deleteClient();
  clearConnectionState(false);
  finishActionWithoutConnectedClient(action, false);
}

const char *disconnectReasonText(int reason) {
  switch (reason & 0xFF) {
    case 0x08: return "supervision timeout";
    case 0x13: return "terminated by peer";
    case 0x16: return "terminated by local host";
    case 0x22: return "link-layer response timeout";
    case 0x3E: return "connection establishment failed";
    default: return "other";
  }
}

void attemptInPlaceStreamRecovery(const char *reason) {
  if (!connected || client == nullptr || !client->isConnected() ||
      notifyCharacteristic == nullptr || writeCharacteristic == nullptr) {
    return;
  }

  ++streamRecoveryAttempts;
  nextStreamProbeAt = TimeUtils::deadlineAfter(
      millis(), AppConfig::Ble::StatusProbePeriodMs);
  resetAllProtocolDecoders();
  clearNotificationQueue();

  const uint32_t now = millis();
  const uint32_t rawAt = lastNotificationReceivedAt;
  const bool streamSilent = rawAt == 0U ||
      TimeUtils::elapsedSince(now, rawAt) >= AppConfig::Ble::StatusProbeAfterMs;
  const bool useNotify = notifyCharacteristic->canNotify();
  const bool canSubscribe = useNotify || notifyCharacteristic->canIndicate();
  const bool subscribed = !streamSilent ||
      (canSubscribe && notifyCharacteristic->subscribe(
                           useNotify, notifyCallback, true));
  const bool requested = sendStatusRequestForActiveType();

  DiagnosticLog::printf(
      "[%lu ms] %s stream recovery #%u: %s, subscribe=%s, request=%s.\n",
      static_cast<unsigned long>(millis()),
      BmsTypeInfo::key(activeConnectionType),
      static_cast<unsigned>(streamRecoveryAttempts),
      reason == nullptr ? "status delayed" : reason,
      subscribed ? "ok" : "failed",
      requested ? "ok" : "failed");
}

void requestConnectionRecovery(const char *reason) {
  if (pendingAction != PendingAction::None || waitingForActionDisconnect) return;
  DiagnosticLog::printf("[%lu ms] %s; restarting BLE link.\n",
                        static_cast<unsigned long>(millis()),
                        reason);
  pendingAction = PendingAction::RecoverLink;
}

void handleTargetEvent() {
  targetPeerAddressValid = true;
  linkState = LinkState::ConnectScheduled;
  connectAt = TimeUtils::deadlineAfter(millis(), AppConfig::Ble::ConnectDelayMs);
  DiagnosticLog::printf("[%lu ms] Found target BMS %s MAC=%s RSSI=%d dBm.\n",
                        static_cast<unsigned long>(millis()),
                        targetName,
                        targetAddress,
                        connectionRssi);
}

void handleScanEndEvent(ScanMode mode, int reason) {

  if (mode == ScanMode::Reconnect &&
      (targetPeerAddressValid || connectAt != 0U || connected)) {
    return;
  }

  linkState = LinkState::Idle;
  if (mode == ScanMode::Pairing) {
    DiagnosticLog::printf("[%lu ms] Web configuration scan finished, reason=%d, devices=%u.\n",
                          static_cast<unsigned long>(millis()),
                          reason,
                          static_cast<unsigned>(scanDeviceCount));

    configurationScanComplete = true;
    return;
  }

  DiagnosticLog::printf("[%lu ms] Reconnect scan ended, reason=%d; target not found.\n",
                        static_cast<unsigned long>(millis()),
                        reason);
  scheduleReconnectScan(AppConfig::Ble::ScanRetryDelayMs);
}

void processCallbackEvents() {
  int reason = 0;
  uint32_t eventAt = 0;
  if (takeDisconnectEvent(reason, eventAt)) {
    handleDisconnectEvent(reason, eventAt);
  }

  if (takeTargetEvent()) {
    handleTargetEvent();
  }

  ScanMode endedMode = ScanMode::None;
  if (takeScanEndEvent(endedMode, reason)) {
    handleScanEndEvent(endedMode, reason);
  }
}

}

bool begin() {
  if (initialized) return true;

  PairedDeviceStore::load(pairedType,
                          pairedAddress,
                          sizeof(pairedAddress),
                          pairedName,
                          sizeof(pairedName));
  configurationType = pairedType;
  activeConnectionType = pairedType;
  if (pairedAddress[0] != '\0' && !isValidMacAddress(pairedAddress)) {
    DiagnosticLog::write("Stored BLE address is invalid; clearing target record.\n");
    pairedAddress[0] = '\0';
    pairedName[0] = '\0';
    PairedDeviceStore::clear();
  }

  initialized = true;

#if defined(CONFIG_ESP_COEX_SW_COEXIST_ENABLE) && CONFIG_ESP_COEX_SW_COEXIST_ENABLE
  DiagnosticLog::write("ESP-IDF software Wi-Fi/BLE coexistence is enabled.\n");
#else
  DiagnosticLog::write(
      "WARNING: ESP-IDF software Wi-Fi/BLE coexistence is disabled in this core.\n");
#endif

  if (!hasStoredTarget()) {
    DiagnosticLog::write("No BMS target stored; waiting for Web configuration.\n");
  } else if (pairedAddress[0] != '\0') {
    DiagnosticLog::printf("Stored %s target loaded: %s (%s).\n",
                          BmsTypeInfo::key(pairedType), pairedName, pairedAddress);
  } else {
    DiagnosticLog::printf("Stored %s target name loaded: %s; MAC will be learned after valid data.\n",
                          BmsTypeInfo::key(pairedType), pairedName);
  }
  return true;
}

void setWebPortalActive(bool active) {
  if (!initialized) return;
  if (webPortalStateKnown && webPortalActive == active) return;

  const bool firstStateSync = !webPortalStateKnown;
  const bool portalJustClosed = webPortalStateKnown && webPortalActive && !active;
  webPortalStateKnown = true;
  webPortalActive = active;

  if (active) {

    stopScanIfRunning();
    reconnectAt = 0;
    fastReconnectAt = 0;
    connectAt = 0;
    targetPeerAddressValid = false;

    if (client != nullptr && client->isConnected()) {
      if (!waitingForActionDisconnect) pendingAction = PendingAction::PauseForWeb;
    } else {
      pendingAction = PendingAction::None;
      waitingForActionDisconnect = false;
      actionDisconnectDeadlineAt = 0;
      clearConnectionState(false);
      deleteClient();
      linkState = LinkState::Idle;
    }

    DiagnosticLog::write("Web portal active: BLE radio work is fully suspended.\n");
    return;
  }

  webPortalResumeAt =
      portalJustClosed
          ? TimeUtils::deadlineAfter(
                millis(), AppConfig::WebConfig::BleResumeAfterClientLeavesMs)
          : 0U;
  DiagnosticLog::write("Web portal off: BLE reconnect may resume.\n");

  if (!connected && pendingAction == PendingAction::None && hasStoredTarget()) {
    scheduleReconnectScan(firstStateSync ? AppConfig::Ble::BootReconnectDelayMs
                                         : AppConfig::Runtime::ResumeReconnectDelayMs);
  }
}

void loop() {
  if (!initialized) return;
  const uint32_t now = millis();

  processCallbackEvents();
  processNotificationQueue();

  if (pendingAction != PendingAction::None && !waitingForActionDisconnect) {
    performAction(pendingAction);
  }

  if (waitingForActionDisconnect &&
      TimeUtils::deadlineReached(now, actionDisconnectDeadlineAt)) {
    handleActionDisconnectTimeout();
  }

  if (automaticBleAllowed() && TimeUtils::deadlineReached(now, fastReconnectAt)) {
    fastReconnectAt = 0;
    ++fastReconnectAttempts;

    if (!connectCachedClient()) {
      clearConnectionState(false);
      deleteClient();

      if (fastReconnectAttempts < AppConfig::Ble::FastReconnectAttempts) {
        fastReconnectAt = TimeUtils::deadlineAfter(
            millis(), AppConfig::Ble::FastReconnectDelayMs);
      } else {
        scheduleReconnectScan(AppConfig::Ble::ScanRetryDelayMs);
      }
    }
  }

  if (automaticBleAllowed() && TimeUtils::deadlineReached(now, connectAt)) {
    connectAt = 0;
    if (!connectFreshTarget()) {
      clearConnectionState(false);
      deleteClient();
      scheduleReconnectScan(AppConfig::Ble::ScanRetryDelayMs);
    }
  }

  if (connected) {
    if (client == nullptr || !client->isConnected()) {
      requestConnectionRecovery("NimBLE reports link no longer connected");
    } else {

      if (activeConnectionType == BmsType::Jiabaida &&
          TimeUtils::deadlineReached(now, nextAuxRequestAt)) {
        nextAuxRequestAt = 0;
        sendJbdCommand(0x04, "JBD cell voltage request");
      }
    }

    if (client == nullptr || !client->isConnected()) {

    } else if (!firstStatusReceived) {
      if (TimeUtils::deadlineReached(now, nextStatusAt)) {
        sendInitialStatusRequest(now);
      }

      if (TimeUtils::deadlineReached(now, firstStatusDeadlineAt)) {
        const uint32_t rawAt = lastNotificationReceivedAt;
        const uint32_t rawAge =
            rawAt == 0U ? TimeUtils::elapsedSince(now, connectionReadyAt)
                        : TimeUtils::elapsedSince(now, rawAt);
        if (consecutiveWriteFailures >= AppConfig::Ble::MaxConsecutiveWriteFailures &&
            rawAge >= AppConfig::Ble::RawNotificationDeadMs) {
          requestConnectionRecovery("No BMS notifications and repeated request failures");
        } else {
          attemptInPlaceStreamRecovery("first valid status still pending");
          firstStatusDeadlineAt = TimeUtils::deadlineAfter(now, AppConfig::Ble::FirstStatusTimeoutMs);
        }
      }
    } else {
      if (TimeUtils::deadlineReached(now, nextDeviceInfoAt)) {
        nextDeviceInfoAt = 0;
        sendDeviceInfoRequestForActiveType();
      }

      if (typeRequiresPeriodicPolling(activeConnectionType) &&
          TimeUtils::deadlineReached(now, nextStatusAt)) {
        nextStatusAt = TimeUtils::deadlineAfter(
            now, AppConfig::Ble::StatusRequestPeriodMs);
        sendStatusRequestForActiveType();
      }

      const uint32_t statusAge = TimeUtils::elapsedSince(now, lastStatusReceivedAt);
      if (statusAge >= AppConfig::Ble::StatusProbeAfterMs &&
          TimeUtils::deadlineReached(now, nextStreamProbeAt)) {
        attemptInPlaceStreamRecovery("valid status delayed");
      }

      if (statusAge >= AppConfig::Ble::StatusHardRecoveryMs) {
        const uint32_t rawAt = lastNotificationReceivedAt;
        const uint32_t rawAge =
            rawAt == 0U ? TimeUtils::elapsedSince(now, connectionReadyAt)
                        : TimeUtils::elapsedSince(now, rawAt);

        if (rawAge >= AppConfig::Ble::RawNotificationDeadMs ||
            consecutiveWriteFailures >= AppConfig::Ble::MaxConsecutiveWriteFailures) {
          requestConnectionRecovery("BMS stream silent after in-place recovery");
        } else {
          nextStreamProbeAt = TimeUtils::deadlineAfter(now, AppConfig::Ble::StatusProbePeriodMs);
        }
      }
    }
  }

  if (automaticBleAllowed() && TimeUtils::deadlineReached(now, reconnectAt)) {
    reconnectAt = 0;
    startScan(false);
  }
}

bool isConnected() {
  return connected;
}

bool hasValidStatus() {
  return connected && firstStatusReceived;
}

void requestConfigurationScan(BmsType type) {
  if (!initialized) return;
  configurationType = type;
  clearScanResults();
  configurationScanComplete = false;
  pendingAction = PendingAction::StartPairingScan;
}

void cancelConfigurationScan() {
  if (!initialized) return;
  stopScanIfRunning();
  configurationScanComplete = true;
  pendingAction = PendingAction::None;
}

bool saveConfiguredTargetForRestart(BmsType type,
                                    const char *address,
                                    const char *name) {
  if (!initialized) return false;
  if (address == nullptr) address = "";
  if (name == nullptr) name = "";

  if (address[0] != '\0' && !isValidMacAddress(address)) return false;
  if (address[0] == '\0' && name[0] == '\0') return false;
  if (strlen(name) > AppConfig::Ble::MaxStoredDeviceNameLength) return false;

  char selectedName[sizeof(pairedName)] = {};
  snprintf(selectedName,
           sizeof(selectedName),
           "%s",
           name[0] != '\0' ? name : BmsTypeInfo::defaultDeviceName(type));

  if (!PairedDeviceStore::save(type, address, selectedName)) return false;

  pairedType = type;
  activeConnectionType = type;
  configurationType = type;
  snprintf(pairedAddress, sizeof(pairedAddress), "%s", address);
  snprintf(pairedName, sizeof(pairedName), "%s", selectedName);
  DiagnosticLog::printf(
      "Web target stored for reboot: type=%s, name=%s, MAC=%s.\n",
      BmsTypeInfo::key(pairedType),
      pairedName,
      pairedAddress[0] == '\0' ? "learn by name" : pairedAddress);
  return true;
}

bool hasConfiguredDevice() {
  return initialized && hasStoredTarget();
}

bool clearPairedDevice() {
  pairedAddress[0] = '\0';
  pairedName[0] = '\0';
  pairedType = configurationType;
  reconnectAt = 0;
  fastReconnectAt = 0;
  connectAt = 0;
  pendingAction = PendingAction::None;
  configurationScanComplete = true;
  return PairedDeviceStore::clear();
}

size_t copyScanResults(ScanDevice *destination, size_t capacity) {
  if (destination == nullptr || capacity == 0U) return 0U;

  portENTER_CRITICAL(&scanResultsMux);
  const size_t count = min(capacity, scanDeviceCount);
  for (size_t i = 0; i < count; ++i) destination[i] = scanDevices[i];
  portEXIT_CRITICAL(&scanResultsMux);

  for (size_t i = 1; i < count; ++i) {
    ScanDevice item = destination[i];
    size_t position = i;
    while (position > 0U) {
      const ScanDevice &previous = destination[position - 1U];
      if (previous.rssi >= item.rssi) break;
      destination[position] = previous;
      --position;
    }
    destination[position] = item;
  }
  return count;
}

bool isConfigurationScanComplete() {
  return initialized && configurationScanComplete;
}

bool prepareForWebPortal() {
  if (!initialized) return false;

  stopScanIfRunning();
  clearPendingCallbackEvents();
  pendingAction = PendingAction::None;
  waitingForActionDisconnect = false;
  actionDisconnectDeadlineAt = 0;
  reconnectAt = 0;
  fastReconnectAt = 0;
  connectAt = 0;
  targetPeerAddressValid = false;

  if (client != nullptr && client->isConnected()) {
    client->disconnect();
    delay(AppConfig::Ble::CallbackExitSettleMs);
  }
  clearConnectionState(false);
  deleteClient();
  linkState = LinkState::Idle;

  portENTER_CRITICAL(&eventMux);
  disconnectEventPending = false;
  targetEventPending = false;
  scanEndEventPending = false;
  callbackScanMode = ScanMode::None;
  callbackScanActive = false;
  portEXIT_CRITICAL(&eventMux);

  if (radioInitialized) {
    const bool stopped = NimBLEDevice::deinit(true);
    radioInitialized = false;
    client = nullptr;
    writeCharacteristic = nullptr;
    notifyCharacteristic = nullptr;
    if (!stopped) {
      DiagnosticLog::write("NimBLE deinit failed before Web AP.\n");
      return false;
    }
    DiagnosticLog::write("NimBLE controller released before SoftAP startup.\n");
  }

  return true;
}

const char *pairedDeviceAddress() {
  return pairedAddress;
}

BmsType pairedBmsType() {
  return pairedType;
}

BmsType configurationScanType() {
  return configurationType;
}

}
