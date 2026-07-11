#pragma once

#include <Arduino.h>
#include <SD.h>
#include "Uf2ValidationCore.h"

struct Uf2ValidationInfo {
  Uf2ValidationResult result = Uf2ValidationResult::OpenFailed;
  size_t fileSize = 0;
  uint32_t familyId = 0;
  bool hasFamilyId = false;
};

class Uf2Validator {
public:
  static Uf2ValidationInfo validate(const String& path, Uf2Target target);
  static const char* message(Uf2ValidationResult result);
};
