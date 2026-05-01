#include <Arduino.h>
#include <Preferences.h>

#include "flash_manager.h"
#include "package_store.h"
#include "stm32_swd_debug.h"
#include "stm32f1_flash.h"
#include "stm32f1_swd_backend.h"
#include "stm32f4_flash.h"
#include "stm32fx_swd_backend.h"
#include "stm32h7_flash.h"
#include "stm32h7_swd_backend.h"
#include "target_control.h"
#include "hal/swd_transport.h"
#include "ap_manager.h"
#include "display/display_manager.h"
#include "input/input_manager.h"
#include "web_server.h"

namespace {
Preferences preferences;
PackageStore packageStore;
TargetControl targetControl;
SwdTransport swdTransport(AppConfig::kSwdIoPin, AppConfig::kSwdClockPin);
Stm32SwdDebug stm32SwdDebug(swdTransport, targetControl);
Stm32F1Flash stm32F1Flash(stm32SwdDebug);
Stm32F4Flash stm32F4Flash(stm32SwdDebug);
Stm32H7Flash stm32H7Flash(stm32SwdDebug);
Stm32F1SwdBackend stm32F1SwdBackend(targetControl, swdTransport, stm32SwdDebug, stm32F1Flash);
Stm32FxSwdBackend stm32F4SwdBackend(targetControl, swdTransport, stm32SwdDebug, stm32F4Flash, Stm32Family::F4);
Stm32FxSwdBackend stm32F7SwdBackend(targetControl, swdTransport, stm32SwdDebug, stm32F4Flash, Stm32Family::F7);
Stm32H7SwdBackend stm32H7SwdBackend(targetControl, swdTransport, stm32SwdDebug, stm32H7Flash);
FlashManager flashManager(packageStore, targetControl, stm32SwdDebug, stm32F1SwdBackend, stm32F4SwdBackend, stm32F7SwdBackend, stm32H7SwdBackend, preferences);
AccessPointManager accessPointManager;
DisplayManager displayManager;
InputManager inputManager(packageStore, flashManager);
AppWebServer webServer(accessPointManager, packageStore, flashManager, targetControl, stm32SwdDebug);
uint32_t lastChipProbeMs = 0;
uint32_t lastDetectedChipId = 0;

void flashAction(void *context) {
  static_cast<InputManager *>(context)->flashSelectedPackage();
}

void nextAction(void *context) {
  static_cast<InputManager *>(context)->selectNextPackage();
}

void previousAction(void *context) {
  static_cast<InputManager *>(context)->selectPreviousPackage();
}

void updateDetectedChip() {
  if (flashManager.isBusy()) {
    return;
  }

  const uint32_t now = millis();
  if (now - lastChipProbeMs < 2000) {
    return;
  }
  lastChipProbeMs = now;

  String error;
  if (!targetControl.prepareForSwd(error) || !stm32SwdDebug.connect(error)) {
    if (lastDetectedChipId != 0) {
      lastDetectedChipId = 0;
      flashManager.clearDetectedChip();
    }
    return;
  }

  uint32_t dbgmcuIdcode = 0;
  if (!stm32SwdDebug.readStm32DebugId(dbgmcuIdcode, error)) {
    if (lastDetectedChipId != 0) {
      lastDetectedChipId = 0;
      flashManager.clearDetectedChip();
    }
    return;
  }

  const uint32_t chipId = dbgmcuIdcode & 0x0FFFU;
  if (chipId != lastDetectedChipId || !flashManager.status().detectedChip.length()) {
    lastDetectedChipId = chipId;
    flashManager.setDetectedChip(chipId);
  }
}

DisplaySnapshot makeDisplaySnapshot() {
  FlashStatus status = flashManager.status();
  DisplaySnapshot snapshot;
  snapshot.stateLabel = status.stateLabel;
  snapshot.message = status.message;
  snapshot.log = status.log;
  snapshot.targetChip = status.targetChip;
  snapshot.detectedChip = status.detectedChip;
  snapshot.flashBackend = status.flashBackend;
  snapshot.selectedPackageName = inputManager.selectedPackageName();
  snapshot.selectedPackageId = inputManager.selectedPackageId();
  snapshot.selectedPackageChip = inputManager.selectedPackageChip();
  snapshot.selectedPackageAddress = inputManager.selectedPackageAddress();
  snapshot.selectedPackageCrc32 = inputManager.selectedPackageCrc32();
  snapshot.selectedPackageSize = inputManager.selectedPackageSize();
  snapshot.selectedPackageIndex = inputManager.selectedPackageIndex();
  snapshot.savedPackageCount = inputManager.savedPackageCount();
  snapshot.uiMessage = inputManager.uiMessage();
  snapshot.targetAddress = status.targetAddress;
  snapshot.firmwareCrc32 = status.firmwareCrc32;
  snapshot.bytesWritten = status.bytesWritten;
  snapshot.totalBytes = status.totalBytes;
  snapshot.packageReady = status.packageReady;
  snapshot.flashBusy = flashManager.isBusy();
  return snapshot;
}
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("Exlink STM32 Wireless Flasher");

  targetControl.begin();

  if (!packageStore.begin()) {
    Serial.println("LittleFS init failed");
  }

  String error;
  if (!accessPointManager.begin(error)) {
    Serial.print("AP start failed: ");
    Serial.println(error);
  } else {
    Serial.print("AP ready at http://");
    Serial.println(accessPointManager.ipAddress());
  }

  flashManager.begin();
  displayManager.begin();
  displayManager.onFlash(flashAction, &inputManager);
  displayManager.onNext(nextAction, &inputManager);
  displayManager.onPrevious(previousAction, &inputManager);
  inputManager.begin();
  webServer.begin();
}

void loop() {
  webServer.handleClient();
  inputManager.update();
  updateDetectedChip();
  displayManager.update(makeDisplaySnapshot());
  delay(2);
}
