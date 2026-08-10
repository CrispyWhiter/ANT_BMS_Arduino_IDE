#pragma once

#include "../bms_model.h"

namespace BmsDataStore {

void reset();

void invalidate();

void publishStatus(const BmsData &data);

void publishDeviceInfo(const char *hardwareVersion, const char *softwareVersion);

bool consumePendingUpdate(BmsData &data);

}
