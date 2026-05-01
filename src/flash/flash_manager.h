#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include "flash_backend.h"
#include "flash_manifest.h"
#include "stm32_chip_info.h"

class PackageStore;
class TargetControl;
class Stm32SwdDebug;

class FlashBackend;

enum class FlashState {
  Idle,
  UploadReady,
  PreparingTarget,
  ConnectingSwd,
  HaltingTarget,
  Erasing,
  Writing,
  Verifying,
  Success,
  Error,
  Cancelled,
};

struct FlashStatus {
  FlashState state = FlashState::Idle;
  String stateLabel = "idle";
  String message = "Ready";
  String log = "Ready";
  String targetChip = "";
  String detectedChip = "";
  String flashBackend = "";
  uint32_t detectedChipId = 0;
  uint32_t targetAddress = 0;
  uint32_t firmwareCrc32 = 0;
  size_t bytesWritten = 0;
  size_t totalBytes = 0;
  bool packageReady = false;
};

class FlashManager {
public:
  FlashManager(PackageStore &packageStore,
               TargetControl &targetControl,
               Stm32SwdDebug &debug,
               FlashBackend &f1Backend,
               FlashBackend &f4Backend,
               FlashBackend &f7Backend,
               FlashBackend &h7Backend,
               Preferences &preferences);

  void begin();
  FlashStatus status();
  bool isBusy();
  static bool isBusyState(FlashState state);
  bool setPackageReady(String &error);
  bool startFlash(String &error);
  void clearPackageState();
  void cancel();
  void setDetectedChip(uint32_t chipId);
  void clearDetectedChip();

private:
  PackageStore &packageStore_;
  TargetControl &targetControl_;
  Stm32SwdDebug &debug_;
  FlashBackend &f1Backend_;
  FlashBackend &f4Backend_;
  FlashBackend &f7Backend_;
  FlashBackend &h7Backend_;
  Preferences &preferences_;
  portMUX_TYPE mutex_ = portMUX_INITIALIZER_UNLOCKED;
  TaskHandle_t taskHandle_ = nullptr;
  volatile bool jobQueued_ = false;
  volatile bool cancelRequested_ = false;
  FlashStatus status_;

  static void workerEntry(void *context);
  void workerLoop();
  bool flashProgress(size_t bytesWritten, size_t totalBytes, const char *message);
  void updateDetectedChip(uint32_t chipId);
  FlashBackend *backendForFamily(Stm32Family family);
  void setState(FlashState state, const String &message);
  const char *stateName(FlashState state) const;
};
