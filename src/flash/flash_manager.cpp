#include "flash_manager.h"

#include <LittleFS.h>
#include "app_config.h"
#include "hal/target_control.h"
#include "package_store.h"

namespace {
constexpr const char *kPrefsNamespace = "flasher";

FlashState progressStateForMessage(const char *message, size_t bytesWritten, size_t totalBytes) {
  const String text = message ? String(message) : String();
  if (text.indexOf("verif") >= 0) {
    return FlashState::Verifying;
  }
  if (text.indexOf("program") >= 0 || text.indexOf("Writing") >= 0 || bytesWritten > 0) {
    return FlashState::Writing;
  }
  if (text.indexOf("erase") >= 0) {
    return FlashState::Erasing;
  }
  if (text.indexOf("halt") >= 0) {
    return FlashState::HaltingTarget;
  }
  if (totalBytes > 0) {
    return FlashState::ConnectingSwd;
  }
  return FlashState::Writing;
}
}

FlashManager::FlashManager(PackageStore &packageStore,
                           TargetControl &targetControl,
                           FlashBackend &swdBackend,
                           Preferences &preferences)
    : packageStore_(packageStore), targetControl_(targetControl), swdBackend_(swdBackend), preferences_(preferences) {}

void FlashManager::begin() {
  preferences_.begin(kPrefsNamespace, false);
  status_.packageReady = packageStore_.hasPackage();
  if (status_.packageReady) {
    FlashManifest manifest;
    String error;
    if (packageStore_.loadManifest(manifest, error)) {
      portENTER_CRITICAL(&mutex_);
      status_.targetChip = manifest.chip;
      status_.detectedChip = "";
      status_.targetAddress = manifest.address;
      status_.firmwareCrc32 = manifest.crc32;
      status_.totalBytes = manifest.size;
      portEXIT_CRITICAL(&mutex_);
    }
    setState(FlashState::UploadReady, "Firmware package is ready");
  }
  xTaskCreatePinnedToCore(&FlashManager::workerEntry, "flash_worker", 8192, this, 1, &taskHandle_, ARDUINO_RUNNING_CORE);
}

FlashStatus FlashManager::status() {
  portENTER_CRITICAL(&mutex_);
  FlashStatus copy = status_;
  portEXIT_CRITICAL(&mutex_);
  return copy;
}

bool FlashManager::isBusy() {
  return isBusyState(status().state);
}

bool FlashManager::isBusyState(FlashState state) {
  return state == FlashState::PreparingTarget || state == FlashState::ConnectingSwd ||
         state == FlashState::HaltingTarget || state == FlashState::Erasing || state == FlashState::Writing ||
         state == FlashState::Verifying;
}

bool FlashManager::setPackageReady(String &error) {
  if (!packageStore_.hasPackage()) {
    error = "Firmware package is incomplete";
    return false;
  }

  FlashManifest manifest;
  if (!packageStore_.loadManifest(manifest, error)) {
    return false;
  }

  portENTER_CRITICAL(&mutex_);
  status_.packageReady = true;
  status_.targetChip = manifest.chip;
  status_.detectedChip = "";
  status_.targetAddress = manifest.address;
  status_.firmwareCrc32 = manifest.crc32;
  status_.bytesWritten = 0;
  status_.totalBytes = manifest.size;
  portEXIT_CRITICAL(&mutex_);
  setState(FlashState::UploadReady, "Firmware package is ready");
  return true;
}

bool FlashManager::startFlash(String &error) {
  FlashStatus snapshot = status();
  if (!snapshot.packageReady) {
    error = "Upload a valid package first";
    return false;
  }
  if (snapshot.state == FlashState::PreparingTarget || snapshot.state == FlashState::ConnectingSwd ||
      snapshot.state == FlashState::HaltingTarget || snapshot.state == FlashState::Erasing ||
      snapshot.state == FlashState::Writing || snapshot.state == FlashState::Verifying) {
    error = "A flash job is already running";
    return false;
  }

  cancelRequested_ = false;
  jobQueued_ = true;
  setState(FlashState::PreparingTarget, "Preparing target for SWD");
  return true;
}

void FlashManager::clearPackageState() {
  portENTER_CRITICAL(&mutex_);
  status_.packageReady = false;
  status_.targetChip = "";
  status_.detectedChip = "";
  status_.targetAddress = 0;
  status_.firmwareCrc32 = 0;
  status_.bytesWritten = 0;
  status_.totalBytes = 0;
  portEXIT_CRITICAL(&mutex_);
  setState(FlashState::Idle, "Ready");
}

void FlashManager::cancel() {
  cancelRequested_ = true;
}

void FlashManager::setDetectedChip(uint32_t chipId) {
  updateDetectedChip(chipId);
}

void FlashManager::clearDetectedChip() {
  portENTER_CRITICAL(&mutex_);
  status_.detectedChip = "";
  portEXIT_CRITICAL(&mutex_);
}

void FlashManager::workerEntry(void *context) {
  static_cast<FlashManager *>(context)->workerLoop();
}

void FlashManager::workerLoop() {
  for (;;) {
    if (!jobQueued_) {
      vTaskDelay(pdMS_TO_TICKS(50));
      continue;
    }

    jobQueued_ = false;
    cancelRequested_ = false;

    FlashManifest manifest;
    String error;
    if (!packageStore_.loadManifest(manifest, error)) {
      setState(FlashState::Error, error);
      continue;
    }

    portENTER_CRITICAL(&mutex_);
    status_.targetChip = manifest.chip;
    status_.detectedChip = "";
    status_.targetAddress = manifest.address;
    status_.firmwareCrc32 = manifest.crc32;
    status_.bytesWritten = 0;
    status_.totalBytes = manifest.size;
    portEXIT_CRITICAL(&mutex_);

    if (cancelRequested_) {
      setState(FlashState::Cancelled, "Flash job cancelled");
      continue;
    }

    setState(FlashState::PreparingTarget, "Preparing target for SWD");
    if (!targetControl_.prepareForSwd(error)) {
      setState(FlashState::Error, error);
      continue;
    }

    if (cancelRequested_) {
      setState(FlashState::Cancelled, "Flash job cancelled");
      continue;
    }

    setState(FlashState::ConnectingSwd, "Connecting to the STM32 SWD port");
    setState(FlashState::Erasing, "Erasing target flash");

    if (!swdBackend_.flash(manifest, LittleFS, AppConfig::kFirmwarePath,
                           [](size_t bytesWritten, size_t totalBytes, const char *message, void *context) {
                             return static_cast<FlashManager *>(context)->flashProgress(bytesWritten, totalBytes, message);
                           },
                           [](uint32_t chipId, void *context) {
                             static_cast<FlashManager *>(context)->updateDetectedChip(chipId);
                           },
                           this, error)) {
      if (error == "Flashing cancelled") {
        setState(FlashState::Cancelled, "Flash job cancelled");
      } else {
        setState(FlashState::Error, error);
      }
      continue;
    }

    setState(FlashState::Success, "Firmware flashed successfully");
  }
}

bool FlashManager::flashProgress(size_t bytesWritten, size_t totalBytes, const char *message) {
  if (cancelRequested_) {
    return false;
  }

  portENTER_CRITICAL(&mutex_);
  status_.bytesWritten = bytesWritten;
  status_.totalBytes = totalBytes;
  const FlashState progressState = progressStateForMessage(message, bytesWritten, totalBytes);
  status_.state = progressState;
  status_.stateLabel = stateName(progressState);
  status_.message = message && message[0] ? message : "Writing firmware";
  if (message && message[0]) {
    if (status_.log.length() > 0) {
      status_.log += "\n";
    }
    status_.log += message;
  }
  portEXIT_CRITICAL(&mutex_);
  return true;
}

void FlashManager::updateDetectedChip(uint32_t chipId) {
  const String name = chipName(chipId);
  portENTER_CRITICAL(&mutex_);
  const bool changed = status_.detectedChip != name;
  status_.detectedChip = name;
  status_.message = "Connected: " + name;
  if (changed) {
    if (status_.log.length() > 0) {
      status_.log += "\n";
    }
    status_.log += "Detected: " + name;
  }
  portEXIT_CRITICAL(&mutex_);
}

String FlashManager::chipName(uint32_t chipId) const {
  switch (chipId) {
    case 0x0410:
      return "STM32F1 medium-density (0x0410)";
    case 0x0412:
      return "STM32F1 low-density (0x0412)";
    case 0x0414:
      return "STM32F1 high-density (0x0414)";
    case 0x0418:
      return "STM32F1 connectivity line (0x0418)";
    case 0x0420:
      return "STM32F1 value line (0x0420)";
    case 0x0413:
      return "STM32F4 (0x0413)";
    case 0x0419:
      return "STM32F42x/F43x (0x0419)";
    case 0x0411:
      return "STM32F2 (0x0411)";
    case 0x0433:
      return "STM32F4/F7 (0x0433)";
    case 0x0444:
      return "STM32F0 (0x0444)";
    case 0x0440:
      return "STM32F0 small (0x0440)";
    case 0x0448:
      return "STM32F0 value line (0x0448)";
    case 0x0442:
      return "STM32F0x2 (0x0442)";
    case 0x0445:
      return "STM32F04/F07 (0x0445)";
    case 0x0416:
      return "STM32L1 medium-density (0x0416)";
    case 0x0429:
      return "STM32L1 Cat.5 (0x0429)";
    case 0x0415:
      return "STM32L1 high-density (0x0415)";
    case 0x0435:
      return "STM32L4 (0x0435)";
    case 0x0461:
      return "STM32L4+ (0x0461)";
    case 0x0450:
      return "STM32H7 (0x0450)";
    default: {
      String hex = String(chipId, HEX);
      hex.toUpperCase();
      while (hex.length() < 4) {
        hex = "0" + hex;
      }
      return "STM32 chip ID 0x" + hex;
    }
  }
}

void FlashManager::setState(FlashState state, const String &message) {
  portENTER_CRITICAL(&mutex_);
  status_.state = state;
  status_.stateLabel = stateName(state);
  status_.message = message;
  if (state == FlashState::PreparingTarget) {
    status_.log = message;
  } else if (message.length() > 0) {
    if (status_.log.length() > 0) {
      status_.log += "\n";
    }
    status_.log += message;
  }
  portEXIT_CRITICAL(&mutex_);
}

const char *FlashManager::stateName(FlashState state) const {
  switch (state) {
    case FlashState::Idle:
      return "idle";
    case FlashState::UploadReady:
      return "upload_ready";
    case FlashState::PreparingTarget:
      return "preparing_target";
    case FlashState::ConnectingSwd:
      return "connecting_swd";
    case FlashState::HaltingTarget:
      return "halting_target";
    case FlashState::Erasing:
      return "erasing";
    case FlashState::Writing:
      return "writing";
    case FlashState::Verifying:
      return "verifying";
    case FlashState::Success:
      return "success";
    case FlashState::Error:
      return "error";
    case FlashState::Cancelled:
      return "cancelled";
  }
  return "unknown";
}
