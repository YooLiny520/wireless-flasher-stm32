#include "target_control.h"

void TargetControl::begin() {
  pinMode(AppConfig::kSwdClockPin, OUTPUT);
  digitalWrite(AppConfig::kSwdClockPin, HIGH);
  pinMode(AppConfig::kSwdIoPin, INPUT_PULLUP);
  if (AppConfig::kSwdResetPin >= 0) {
    pinMode(AppConfig::kSwdResetPin, OUTPUT);
    digitalWrite(AppConfig::kSwdResetPin, HIGH);
  }
}

bool TargetControl::prepareForSwd(String &error) {
  (void)error;
  if (AppConfig::kSwdResetPin >= 0) {
    digitalWrite(AppConfig::kSwdResetPin, HIGH);
  }
  delay(2);
  return true;
}

void TargetControl::holdSwdReset() {
  if (AppConfig::kSwdResetPin >= 0) {
    digitalWrite(AppConfig::kSwdResetPin, LOW);
  }
}

void TargetControl::releaseSwdReset() {
  if (AppConfig::kSwdResetPin >= 0) {
    digitalWrite(AppConfig::kSwdResetPin, HIGH);
  }
}

void TargetControl::resetTarget() {
  if (AppConfig::kSwdResetPin >= 0) {
    digitalWrite(AppConfig::kSwdResetPin, LOW);
    delay(20);
    digitalWrite(AppConfig::kSwdResetPin, HIGH);
    delay(20);
  }
}
