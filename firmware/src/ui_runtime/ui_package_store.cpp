#include "ui_package_store.h"

#include <SPIFFS.h>

#include "bmsui_parser.h"
#include "../core/diagnostic_log.h"

namespace UiPackageStore {
namespace {

bool fileSystemMounted = false;
BmsUi::Package validationPackage;
char storageError[112] = "not mounted";
uint8_t copyScratch[512] = {};

void setStorageError(const char *message) {
  snprintf(storageError,
           sizeof(storageError),
           "%s",
           message == nullptr ? "unknown storage error" : message);
}

void clearStorageError() {
  snprintf(storageError, sizeof(storageError), "ok");
}

bool mountExisting() {
  if (fileSystemMounted) return true;

  fileSystemMounted = SPIFFS.begin(false);
  if (!fileSystemMounted) {
    setStorageError("SPIFFS mount failed (not formatted)");
    DiagnosticLog::write(
        "Dynamic UI: SPIFFS mount failed; preserving flash contents and using factory UI.\n");
    return false;
  }

  clearStorageError();
  return true;
}

bool mountWritable() {
  if (mountExisting()) return true;

  DiagnosticLog::write(
      "Dynamic UI: writable operation requested; formatting SPIFFS once because mount failed.\n");
  SPIFFS.end();
  fileSystemMounted = false;
  fileSystemMounted = SPIFFS.begin(true);
  if (!fileSystemMounted) {
    setStorageError("SPIFFS format/mount failed");
    DiagnosticLog::write("Dynamic UI: SPIFFS format/mount failed.\n");
    return false;
  }

  clearStorageError();
  return true;
}

bool parsePathMounted(const char *path,
                      BmsUi::Package &package,
                      BmsUi::ParseResult &result) {
  File file = SPIFFS.open(path, FILE_READ);
  if (!file) {
    result.ok = false;
    result.line = 0;
    snprintf(result.message, sizeof(result.message), "UI package file not found");
    return false;
  }

  if (file.size() == 0U || file.size() > BmsUi::MaxPackageBytes) {
    result.ok = false;
    result.line = 0;
    snprintf(result.message, sizeof(result.message), "UI package size is invalid");
    file.close();
    return false;
  }

  const bool ok = BmsUi::parse(file, package, result);
  file.close();
  return ok;
}

bool parsePath(const char *path,
               BmsUi::Package &package,
               BmsUi::ParseResult &result) {
  if (!mountExisting()) {
    result.ok = false;
    result.line = 0;
    snprintf(result.message, sizeof(result.message), "%s", storageError);
    return false;
  }
  return parsePathMounted(path, package, result);
}

size_t fileSizeMounted(const char *path) {
  if (!SPIFFS.exists(path)) return 0U;
  File file = SPIFFS.open(path, FILE_READ);
  if (!file) return 0U;
  const size_t size = file.size();
  file.close();
  return size;
}

bool copyFileMounted(const char *source,
                     const char *destination,
                     BmsUi::ParseResult &result,
                     const char *failureText) {
  File input = SPIFFS.open(source, FILE_READ);
  if (!input) {
    result.ok = false;
    result.line = 0;
    snprintf(result.message, sizeof(result.message), "unable to open source package");
    return false;
  }

  const size_t expected = input.size();
  SPIFFS.remove(destination);
  File output = SPIFFS.open(destination, FILE_WRITE);
  if (!output) {
    input.close();
    result.ok = false;
    result.line = 0;
    snprintf(result.message, sizeof(result.message), "%s", failureText);
    return false;
  }

  size_t written = 0U;
  bool ok = true;
  while (input.available()) {
    const size_t count = input.read(copyScratch, sizeof(copyScratch));
    if (count == 0U) {
      ok = false;
      break;
    }
    if (output.write(copyScratch, count) != count) {
      ok = false;
      break;
    }
    written += count;
#if defined(ARDUINO)
    delay(0);
#endif
  }
  output.flush();
  output.close();
  input.close();

  if (!ok || written != expected || fileSizeMounted(destination) != expected) {
    SPIFFS.remove(destination);
    result.ok = false;
    result.line = 0;
    snprintf(result.message, sizeof(result.message), "%s", failureText);
    return false;
  }
  return true;
}

bool promoteCandidateMounted(const char *candidate,
                             BmsUi::ParseResult &result,
                             const char *reasonText) {
  if (!SPIFFS.exists(candidate)) return false;
  if (!parsePathMounted(candidate, validationPackage, result)) return false;

  DiagnosticLog::printf("Dynamic UI: recovering valid %s (%u bytes).\n",
                        reasonText,
                        static_cast<unsigned>(fileSizeMounted(candidate)));

  if (!copyFileMounted(candidate,
                       BmsUi::CurrentPath,
                       result,
                       "unable to recover package into /ui.bmsui")) {
    return false;
  }
  if (!parsePathMounted(BmsUi::CurrentPath, validationPackage, result)) {
    SPIFFS.remove(BmsUi::CurrentPath);
    return false;
  }

  SPIFFS.remove(BmsUi::StagingPath);
  SPIFFS.remove(BmsUi::BackupPath);
  clearStorageError();
  DiagnosticLog::write("Dynamic UI: interrupted package recovery completed.\n");
  return true;
}

bool cleanupOrRecoverMounted(BmsUi::ParseResult &result) {
  const bool currentPresent = SPIFFS.exists(BmsUi::CurrentPath);
  if (currentPresent) {
    BmsUi::ParseResult currentResult;
    if (parsePathMounted(BmsUi::CurrentPath, validationPackage, currentResult)) {
      if (SPIFFS.exists(BmsUi::StagingPath)) {
        DiagnosticLog::write("Dynamic UI: removing stale /ui.tmp beside valid current UI.\n");
        SPIFFS.remove(BmsUi::StagingPath);
      }
      if (SPIFFS.exists(BmsUi::BackupPath)) SPIFFS.remove(BmsUi::BackupPath);
      result = currentResult;
      return true;
    }
    DiagnosticLog::printf("Dynamic UI: current package invalid (%s); trying recovery candidates.\n",
                          currentResult.message);
  }

  BmsUi::ParseResult stagingResult;
  if (SPIFFS.exists(BmsUi::StagingPath) &&
      promoteCandidateMounted(BmsUi::StagingPath, stagingResult, "staging package")) {
    result = stagingResult;
    return true;
  }

  BmsUi::ParseResult backupResult;
  if (SPIFFS.exists(BmsUi::BackupPath) &&
      promoteCandidateMounted(BmsUi::BackupPath, backupResult, "backup package")) {
    result = backupResult;
    return true;
  }

  result.ok = false;
  result.line = 0;
  if (SPIFFS.exists(BmsUi::StagingPath)) {
    snprintf(result.message,
             sizeof(result.message),
             "pending /ui.tmp exists but is incomplete/invalid");
  } else if (SPIFFS.exists(BmsUi::BackupPath)) {
    snprintf(result.message,
             sizeof(result.message),
             "backup package exists but is invalid");
  } else {
    snprintf(result.message, sizeof(result.message), "no custom UI package");
  }
  return false;
}

int assetHexValue(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return 10 + c - 'a';
  if (c >= 'A' && c <= 'F') return 10 + c - 'A';
  return -1;
}

int assetBase64Value(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return 26 + c - 'a';
  if (c >= '0' && c <= '9') return 52 + c - '0';
  if (c == '+') return 62;
  if (c == '/') return 63;
  return -1;
}

}  // namespace

bool begin() {
  return mountExisting();
}

bool beginForWrite() {
  return mountWritable();
}

bool mounted() {
  return fileSystemMounted;
}

bool hasCustomPackage() {
  return mountExisting() && SPIFFS.exists(BmsUi::CurrentPath);
}

size_t currentSize() {
  if (!mountExisting()) return 0U;
  return fileSizeMounted(BmsUi::CurrentPath);
}

size_t totalBytes() {
  return mountExisting() ? static_cast<size_t>(SPIFFS.totalBytes()) : 0U;
}

size_t usedBytes() {
  return mountExisting() ? static_cast<size_t>(SPIFFS.usedBytes()) : 0U;
}

size_t stagingSize() {
  if (!mountExisting()) return 0U;
  return fileSizeMounted(BmsUi::StagingPath);
}

size_t backupSize() {
  if (!mountExisting()) return 0U;
  return fileSizeMounted(BmsUi::BackupPath);
}

bool stagingExists() {
  return mountExisting() && SPIFFS.exists(BmsUi::StagingPath);
}

bool backupExists() {
  return mountExisting() && SPIFFS.exists(BmsUi::BackupPath);
}

bool rejectedExists() {
  return mountExisting() && SPIFFS.exists(BmsUi::RejectedPath);
}

size_t rejectedSize() {
  if (!mountExisting()) return 0U;
  return fileSizeMounted(BmsUi::RejectedPath);
}

const char *lastStorageError() {
  return storageError;
}

bool load(BmsUi::Package &package, BmsUi::ParseResult &result) {
  return parsePath(BmsUi::CurrentPath, package, result);
}

bool validatePath(const char *path, BmsUi::ParseResult &result) {
  return parsePath(path, validationPackage, result);
}

bool beginStaging(File &file) {
  if (!mountWritable()) return false;
  SPIFFS.remove(BmsUi::StagingPath);
  file = SPIFFS.open(BmsUi::StagingPath, FILE_WRITE);
  if (!file) {
    setStorageError("unable to open staging file");
    return false;
  }
  DiagnosticLog::write("Dynamic UI upload: staging file opened.\n");
  return true;
}

bool recoverPendingPackage(BmsUi::ParseResult &result) {
  if (!mountWritable()) {
    result.ok = false;
    result.line = 0;
    snprintf(result.message, sizeof(result.message), "%s", storageError);
    return false;
  }
  return cleanupOrRecoverMounted(result) && SPIFFS.exists(BmsUi::CurrentPath);
}

bool quarantineCurrentPackage() {
  if (!mountExisting()) return false;

  SPIFFS.remove(BmsUi::StagingPath);
  SPIFFS.remove(BmsUi::BackupPath);
  SPIFFS.remove(BmsUi::RejectedPath);

  if (SPIFFS.exists(BmsUi::CurrentPath) &&
      !SPIFFS.remove(BmsUi::CurrentPath)) {
    setStorageError("unable to disable crashing /ui.bmsui");
    return false;
  }

  clearStorageError();
  DiagnosticLog::write("Dynamic UI disabled after interrupted activation.\n");
  return true;
}

bool remountAndValidateCurrent(BmsUi::ParseResult &result) {
  SPIFFS.end();
  fileSystemMounted = false;
  delay(40);

  if (!mountExisting()) {
    result.ok = false;
    result.line = 0;
    snprintf(result.message,
             sizeof(result.message),
             "post-write remount failed: %s",
             storageError);
    return false;
  }

  if (!SPIFFS.exists(BmsUi::CurrentPath)) {
    result.ok = false;
    result.line = 0;
    snprintf(result.message,
             sizeof(result.message),
             "post-write remount lost /ui.bmsui");
    setStorageError("post-write remount lost /ui.bmsui");
    return false;
  }

  if (!parsePathMounted(BmsUi::CurrentPath, validationPackage, result)) {
    setStorageError("post-write package validation failed");
    return false;
  }

  clearStorageError();
  return true;
}

bool commitStaging(BmsUi::ParseResult &result) {
  if (!mountWritable()) return false;
  if (!SPIFFS.exists(BmsUi::StagingPath)) {
    result.ok = false;
    result.line = 0;
    snprintf(result.message, sizeof(result.message), "staging file disappeared before commit");
    return false;
  }
  if (!parsePathMounted(BmsUi::StagingPath, validationPackage, result)) {
    return false;
  }
  const size_t stagingBytes = fileSizeMounted(BmsUi::StagingPath);
  SPIFFS.remove(BmsUi::BackupPath);
  const bool hadCurrent = SPIFFS.exists(BmsUi::CurrentPath);
  if (hadCurrent) {
    if (!copyFileMounted(BmsUi::CurrentPath,
                         BmsUi::BackupPath,
                         result,
                         "unable to create backup UI package")) {
      return false;
    }
  }
  if (!copyFileMounted(BmsUi::StagingPath,
                       BmsUi::CurrentPath,
                       result,
                       "unable to copy staging package to /ui.bmsui")) {
    if (hadCurrent && SPIFFS.exists(BmsUi::BackupPath)) {
      BmsUi::ParseResult ignored;
      copyFileMounted(BmsUi::BackupPath,
                      BmsUi::CurrentPath,
                      ignored,
                      "unable to restore backup UI package");
    }
    return false;
  }
  if (!parsePathMounted(BmsUi::CurrentPath, validationPackage, result)) {
    if (hadCurrent && SPIFFS.exists(BmsUi::BackupPath)) {
      BmsUi::ParseResult ignored;
      copyFileMounted(BmsUi::BackupPath,
                      BmsUi::CurrentPath,
                      ignored,
                      "unable to restore backup UI package");
    } else {
      SPIFFS.remove(BmsUi::CurrentPath);
    }
    return false;
  }
  if (!remountAndValidateCurrent(result)) {
    return false;
  }

  if (currentSize() != stagingBytes) {
    result.ok = false;
    result.line = 0;
    snprintf(result.message,
             sizeof(result.message),
             "post-remount size mismatch (%u != %u)",
             static_cast<unsigned>(currentSize()),
             static_cast<unsigned>(stagingBytes));
    return false;
  }
  SPIFFS.remove(BmsUi::StagingPath);
  SPIFFS.remove(BmsUi::BackupPath);
  clearStorageError();
  return true;
}

void discardStaging() {
  if (!mountExisting() || !SPIFFS.exists(BmsUi::StagingPath)) return;
  if (fileSizeMounted(BmsUi::StagingPath) == 0U) SPIFFS.remove(BmsUi::StagingPath);
}

bool resetCustomPackage() {
  if (!mountWritable()) return false;
  SPIFFS.remove(BmsUi::StagingPath);
  SPIFFS.remove(BmsUi::BackupPath);
  SPIFFS.remove(BmsUi::RejectedPath);
  if (!SPIFFS.exists(BmsUi::CurrentPath)) return true;
  return SPIFFS.remove(BmsUi::CurrentPath);
}

void prepareForRestart() {
  if (!fileSystemMounted) return;
  SPIFFS.end();
  fileSystemMounted = false;
}

File openCurrent(const char *mode) {
  if (!mountExisting()) return File();
  return SPIFFS.open(BmsUi::CurrentPath, mode);
}

bool loadAsset(const char *name,
               uint8_t *buffer,
               size_t capacity,
               size_t &bytesWritten) {
  bytesWritten = 0U;
  if (name == nullptr || name[0] == '\0' || buffer == nullptr || capacity == 0U || !mountExisting()) {
    return false;
  }

  File file = SPIFFS.open(BmsUi::CurrentPath, FILE_READ);
  if (!file) return false;

  char line[BmsUi::MaxLineLength + 1] = {};
  bool ok = true;
  uint16_t scannedLines = 0U;
  while (file.available()) {
    const size_t length = file.readBytesUntil('\n', line, BmsUi::MaxLineLength);
#if defined(ARDUINO)
    if ((++scannedLines & 0x0FU) == 0U) delay(0);
#endif
    line[length] = '\0';
    size_t trimmed = strlen(line);
    while (trimmed > 0U && (line[trimmed - 1U] == '\r' || line[trimmed - 1U] == '\n')) {
      line[--trimmed] = '\0';
    }
    bool isHex = false;
    bool isBase64 = false;
    size_t prefixLength = 0U;
    if (strncmp(line, "ASSETDATA|", 10U) == 0) {
      isHex = true;
      prefixLength = 10U;
    } else if (strncmp(line, "ASSETB64|", 9U) == 0) {
      isBase64 = true;
      prefixLength = 9U;
    } else {
      continue;
    }
    char *assetName = line + prefixLength;
    char *separator = strchr(assetName, '|');
    if (separator == nullptr) continue;
    *separator = '\0';
    if (strcmp(assetName, name) != 0) continue;
    const char *encoded = separator + 1U;
    const size_t chars = strlen(encoded);
    if (isHex) {
      if ((chars & 1U) != 0U || bytesWritten + chars / 2U > capacity) {
        ok = false;
        break;
      }
      for (size_t i = 0U; i < chars; i += 2U) {
        const int high = assetHexValue(encoded[i]);
        const int low = assetHexValue(encoded[i + 1U]);
        if (high < 0 || low < 0) {
          ok = false;
          break;
        }
        buffer[bytesWritten++] = static_cast<uint8_t>((high << 4) | low);
      }
    } else if (isBase64) {
      if (chars == 0U || (chars & 3U) != 0U) {
        ok = false;
        break;
      }
      for (size_t i = 0U; i < chars; i += 4U) {
        const int v0 = assetBase64Value(encoded[i]);
        const int v1 = assetBase64Value(encoded[i + 1U]);
        const bool pad2 = encoded[i + 2U] == '=';
        const bool pad3 = encoded[i + 3U] == '=';
        const int v2 = pad2 ? 0 : assetBase64Value(encoded[i + 2U]);
        const int v3 = pad3 ? 0 : assetBase64Value(encoded[i + 3U]);
        if (v0 < 0 || v1 < 0 || (!pad2 && v2 < 0) || (!pad3 && v3 < 0) || (pad2 && !pad3) || ((pad2 || pad3) && i + 4U != chars)) {
          ok = false;
          break;
        }
        if (bytesWritten >= capacity) { ok = false; break; }
        buffer[bytesWritten++] = static_cast<uint8_t>((v0 << 2) | (v1 >> 4));
        if (!pad2) {
          if (bytesWritten >= capacity) { ok = false; break; }
          buffer[bytesWritten++] = static_cast<uint8_t>(((v1 & 0x0F) << 4) | (v2 >> 2));
        }
        if (!pad3) {
          if (bytesWritten >= capacity) { ok = false; break; }
          buffer[bytesWritten++] = static_cast<uint8_t>(((v2 & 0x03) << 6) | v3);
        }
      }
    }
    if (!ok) break;
  }
  file.close();
  return ok && bytesWritten > 0U;
}

}  // namespace UiPackageStore
