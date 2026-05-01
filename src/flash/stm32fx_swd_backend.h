#pragma once

#include "flash_backend.h"
#include "stm32_chip_info.h"

class TargetControl;
class SwdTransport;
class Stm32SwdDebug;
class Stm32F4Flash;

class Stm32FxSwdBackend : public FlashBackend {
public:
  Stm32FxSwdBackend(TargetControl &targetControl,
                    SwdTransport &transport,
                    Stm32SwdDebug &debug,
                    Stm32F4Flash &flash,
                    Stm32Family family);

  const char *transportName() const override;
  bool flash(const FlashManifest &manifest,
             fs::FS &fs,
             const char *firmwarePath,
             FlashProgressCallback progressCallback,
             ChipDetectCallback chipDetectCallback,
             void *context,
             String &error) override;

private:
  TargetControl &targetControl_;
  SwdTransport &transport_;
  Stm32SwdDebug &debug_;
  Stm32F4Flash &flash_;
  Stm32Family family_;
};
