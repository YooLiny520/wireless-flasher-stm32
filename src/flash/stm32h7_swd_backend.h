#pragma once

#include "flash_backend.h"

class TargetControl;
class SwdTransport;
class Stm32SwdDebug;
class Stm32H7Flash;

class Stm32H7SwdBackend : public FlashBackend {
public:
  Stm32H7SwdBackend(TargetControl &targetControl,
                    SwdTransport &transport,
                    Stm32SwdDebug &debug,
                    Stm32H7Flash &flash);

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
  Stm32H7Flash &flash_;
};
