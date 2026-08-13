#pragma once

#include <Arduino.h>
#include <FS.h>

#include "bmsui_protocol.h"

namespace UiPackageStore {

bool begin();

bool beginForWrite();

bool mounted();
bool hasCustomPackage();
size_t currentSize();
size_t totalBytes();
size_t usedBytes();
size_t stagingSize();
size_t backupSize();
bool stagingExists();
bool backupExists();
bool rejectedExists();
size_t rejectedSize();
const char *lastStorageError();

bool load(BmsUi::Package &package, BmsUi::ParseResult &result);
bool validatePath(const char *path, BmsUi::ParseResult &result);

bool beginStaging(File &file);
bool recoverPendingPackage(BmsUi::ParseResult &result);

bool quarantineCurrentPackage();
bool commitStaging(BmsUi::ParseResult &result);
void discardStaging();
bool resetCustomPackage();

bool remountAndValidateCurrent(BmsUi::ParseResult &result);

void prepareForRestart();

File openCurrent(const char *mode = FILE_READ);

bool loadAsset(const char *name, uint8_t *buffer, size_t capacity, size_t &bytesWritten);

}  // namespace UiPackageStore
