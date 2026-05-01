#pragma once

#include <Arduino.h>
#include "app_config.h"

class TargetControl {
public:
  TargetControl() = default;

  void begin();
  bool prepareForSwd(String &error);
  void holdSwdReset();
  void releaseSwdReset();
  void resetTarget();
};
