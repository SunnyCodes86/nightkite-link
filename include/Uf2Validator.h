#pragma once

#include <Arduino.h>
#include <SD.h>

enum class Uf2ValidationResult {
  Ok,
  FileMissing,
  EmptyFile,
  SizeNotAligned,
  OpenFailed,
  ReadFailed,
  InvalidMagic,
};

struct Uf2ValidationInfo {
  Uf2ValidationResult result = Uf2ValidationResult::OpenFailed;
  size_t fileSize = 0;
  uint32_t familyId = 0;
  bool hasFamilyId = false;
};

class Uf2Validator {
public:
  static Uf2ValidationInfo validate(const String& path);
  static const char* message(Uf2ValidationResult result);
};
