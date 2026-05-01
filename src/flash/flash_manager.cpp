#include "flash_manager.h"

#include <LittleFS.h>
#include "app_config.h"
#include "hal/target_control.h"
#include "package_store.h"
#include "stm32_swd_debug.h"

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
                           Stm32SwdDebug &debug,
                           FlashBackend &f1Backend,
                           FlashBackend &f4Backend,
                           FlashBackend &f7Backend,
                           FlashBackend &h7Backend,
                           Preferences &preferences)
    : packageStore_(packageStore),
      targetControl_(targetControl),
      debug_(debug),
      f1Backend_(f1Backend),
      f4Backend_(f4Backend),
      f7Backend_(f7Backend),
      h7Backend_(h7Backend),
      preferences_(preferences) {}

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
      status_.detectedChipId = 0;
      status_.flashBackend = "";
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
  status_.detectedChipId = 0;
  status_.flashBackend = "";
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
  status_.detectedChipId = 0;
  status_.flashBackend = "";
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

    uint32_t chipId = status().detectedChipId;
    if (chipId == 0) {
      setState(FlashState::ConnectingSwd, "Detecting STM32 target");
      if (!debug_.connect(error)) {
        setState(FlashState::Error, "SWD connect failed: " + error);
        continue;
      }
      uint32_t dbgmcuIdcode = 0;
      if (!debug_.readStm32DebugId(dbgmcuIdcode, error)) {
        setState(FlashState::Error, "SWD chip ID read failed: " + error);
        continue;
      }
      chipId = dbgmcuIdcode & 0x0FFFU;
      updateDetectedChip(chipId);
    }
    const Stm32ChipInfo &chip = stm32ChipInfo(chipId);
    FlashBackend *backend = backendForFamily(chip.family);
    if (!backend) {
      setState(FlashState::Error, chipId == 0 ? "Connect STM32 target before flashing" : "Unsupported STM32 flash family: " + stm32ChipDisplayName(chipId));
      continue;
    }
    if (!stm32FamilyMatchesChipName(chip.family, manifest.chip)) {
      setState(FlashState::Error, "Firmware target " + manifest.chip + " does not match detected " + stm32FamilyName(chip.family));
      continue;
    }

    portENTER_CRITICAL(&mutex_);
    status_.flashBackend = backend->transportName();
    portEXIT_CRITICAL(&mutex_);

    setState(FlashState::ConnectingSwd, String("Using ") + backend->transportName() + " backend");
    setState(FlashState::Erasing, "Erasing target flash");

    if (!backend->flash(manifest, LittleFS, AppConfig::kFirmwarePath,
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
  const String name = stm32ChipDisplayName(chipId);
  const Stm32ChipInfo &chip = stm32ChipInfo(chipId);
  FlashBackend *backend = backendForFamily(chip.family);
  portENTER_CRITICAL(&mutex_);
  const bool changed = status_.detectedChipId != chipId || status_.detectedChip != name;
  status_.detectedChip = name;
  status_.detectedChipId = chipId;
  status_.flashBackend = backend ? backend->transportName() : "unsupported";
  status_.message = "Connected: " + name;
  if (changed) {
    if (status_.log.length() > 0) {
      status_.log += "\n";
    }
    status_.log += "Detected: " + name;
  }
  portEXIT_CRITICAL(&mutex_);
}

FlashBackend *FlashManager::backendForFamily(Stm32Family family) {
  switch (family) {
    case Stm32Family::F1:
      return &f1Backend_;
    case Stm32Family::F4:
      return &f4Backend_;
    case Stm32Family::F7:
      return &f7Backend_;
    case Stm32Family::H7:
      return &h7Backend_;
    case Stm32Family::Unknown:
      break;
  }
  return nullptr;
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
