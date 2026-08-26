#include "paired_device_store.h"

#include <Preferences.h>

namespace PairedDeviceStore {
namespace {

constexpr char kNamespace[] = "ant_bms";
constexpr char kAddressKey[] = "address";
constexpr char kNameKey[] = "name";
constexpr char kTypeKey[] = "bms_type";

}

bool load(BmsType &type,
          char *address,
          size_t addressSize,
          char *name,
          size_t nameSize) {
  if (address == nullptr || addressSize == 0U ||
      name == nullptr || nameSize == 0U) {
    return false;
  }

  type = BmsType::Ant;
  address[0] = '\0';
  name[0] = '\0';

  Preferences preferences;
  if (!preferences.begin(kNamespace, true)) return false;
  const uint8_t rawType = preferences.getUChar(
      kTypeKey, static_cast<uint8_t>(BmsType::Ant));
  if (BmsTypeInfo::isValidRaw(rawType)) type = static_cast<BmsType>(rawType);
  preferences.getString(kAddressKey, address, addressSize);
  preferences.getString(kNameKey, name, nameSize);
  preferences.end();

  return address[0] != '\0' || name[0] != '\0';
}

bool save(BmsType type, const char *address, const char *name) {
  if (address == nullptr) address = "";
  if (name == nullptr) name = "";
  if (address[0] == '\0' && name[0] == '\0') return false;

  Preferences preferences;
  if (!preferences.begin(kNamespace, false)) return false;

  const bool typeOk = preferences.putUChar(
      kTypeKey, static_cast<uint8_t>(type)) > 0U;
  bool nameOk = true;
  bool addressOk = true;

  if (name[0] == '\0') preferences.remove(kNameKey);
  else nameOk = preferences.putString(kNameKey, name) > 0U;

  if (address[0] == '\0') preferences.remove(kAddressKey);
  else addressOk = preferences.putString(kAddressKey, address) > 0U;

  preferences.end();
  return typeOk && nameOk && addressOk;
}

bool clear() {
  Preferences preferences;
  if (!preferences.begin(kNamespace, false)) return false;
  preferences.remove(kTypeKey);
  preferences.remove(kAddressKey);
  preferences.remove(kNameKey);
  preferences.end();
  return true;
}

}
