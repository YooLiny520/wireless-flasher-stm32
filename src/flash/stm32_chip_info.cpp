#include "stm32_chip_info.h"

namespace {
constexpr Stm32ChipInfo kUnknownChip = {0, Stm32Family::Unknown, "Unsupported STM32", 0x08000000UL, 0x08200000UL};

constexpr Stm32ChipInfo kChips[] = {
    {0x0410, Stm32Family::F1, "STM32F1 medium-density (0x0410)", 0x08000000UL, 0x08020000UL},
    {0x0412, Stm32Family::F1, "STM32F1 low-density (0x0412)", 0x08000000UL, 0x08008000UL},
    {0x0414, Stm32Family::F1, "STM32F1 high-density (0x0414)", 0x08000000UL, 0x08080000UL},
    {0x0418, Stm32Family::F1, "STM32F1 connectivity line (0x0418)", 0x08000000UL, 0x08040000UL},
    {0x0420, Stm32Family::F1, "STM32F1 value line (0x0420)", 0x08000000UL, 0x08020000UL},
    {0x0413, Stm32Family::F4, "STM32F4 (0x0413)", 0x08000000UL, 0x08100000UL},
    {0x0419, Stm32Family::F4, "STM32F42x/F43x (0x0419)", 0x08000000UL, 0x08200000UL},
    {0x0433, Stm32Family::F4, "STM32F4 (0x0433)", 0x08000000UL, 0x08100000UL},
    {0x0449, Stm32Family::F7, "STM32F74x/F75x (0x0449)", 0x08000000UL, 0x08200000UL},
    {0x0451, Stm32Family::F7, "STM32F76x/F77x (0x0451)", 0x08000000UL, 0x08200000UL},
    {0x0452, Stm32Family::F7, "STM32F72x/F73x (0x0452)", 0x08000000UL, 0x08100000UL},
    {0x0450, Stm32Family::H7, "STM32H7 (0x0450)", 0x08000000UL, 0x08200000UL},
    {0x0480, Stm32Family::H7, "STM32H7A/B (0x0480)", 0x08000000UL, 0x08100000UL},
    {0x0483, Stm32Family::H7, "STM32H72x/H73x (0x0483)", 0x08000000UL, 0x08100000UL},
};

String unknownChipName(uint32_t chipId) {
  String hex = String(chipId, HEX);
  hex.toUpperCase();
  while (hex.length() < 4) {
    hex = "0" + hex;
  }
  return "STM32 chip ID 0x" + hex;
}
}

const Stm32ChipInfo &stm32ChipInfo(uint32_t chipId) {
  for (const Stm32ChipInfo &chip : kChips) {
    if (chip.id == chipId) {
      return chip;
    }
  }
  return kUnknownChip;
}

String stm32ChipDisplayName(uint32_t chipId) {
  const Stm32ChipInfo &chip = stm32ChipInfo(chipId);
  if (chip.family == Stm32Family::Unknown) {
    return unknownChipName(chipId);
  }
  return chip.name;
}

const char *stm32FamilyName(Stm32Family family) {
  switch (family) {
    case Stm32Family::F1:
      return "STM32F1";
    case Stm32Family::F4:
      return "STM32F4";
    case Stm32Family::F7:
      return "STM32F7";
    case Stm32Family::H7:
      return "STM32H7";
    case Stm32Family::Unknown:
      break;
  }
  return "unsupported";
}

bool stm32FamilyMatchesChipName(Stm32Family family, const String &chipName) {
  String normalized = chipName;
  normalized.trim();
  normalized.toLowerCase();
  if (normalized.isEmpty() || normalized == "stm32") {
    return true;
  }
  switch (family) {
    case Stm32Family::F1:
      return normalized.indexOf("f1") >= 0 || normalized.indexOf("stm32f1") >= 0;
    case Stm32Family::F4:
      return normalized.indexOf("f4") >= 0 || normalized.indexOf("stm32f4") >= 0;
    case Stm32Family::F7:
      return normalized.indexOf("f7") >= 0 || normalized.indexOf("stm32f7") >= 0;
    case Stm32Family::H7:
      return normalized.indexOf("h7") >= 0 || normalized.indexOf("stm32h7") >= 0;
    case Stm32Family::Unknown:
      break;
  }
  return false;
}
