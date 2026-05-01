#pragma once

#include <Arduino.h>

class FlashBackend;

enum class Stm32Family {
  Unknown,
  F1,
  F4,
  F7,
  H7,
};

struct Stm32ChipInfo {
  uint32_t id;
  Stm32Family family;
  const char *name;
  uint32_t flashStart;
  uint32_t flashEnd;
};

const Stm32ChipInfo &stm32ChipInfo(uint32_t chipId);
String stm32ChipDisplayName(uint32_t chipId);
const char *stm32FamilyName(Stm32Family family);
bool stm32FamilyMatchesChipName(Stm32Family family, const String &chipName);
