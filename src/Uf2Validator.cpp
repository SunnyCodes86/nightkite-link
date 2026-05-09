#include "Uf2Validator.h"

namespace {

constexpr uint32_t UF2_MAGIC_START0 = 0x0A324655;
constexpr uint32_t UF2_MAGIC_START1 = 0x9E5D5157;
constexpr uint32_t UF2_MAGIC_END = 0x0AB16F30;
constexpr uint32_t UF2_FLAG_FAMILY_ID_PRESENT = 0x00002000;

uint32_t readLe32(const uint8_t* data)
{
  return static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8) |
         (static_cast<uint32_t>(data[2]) << 16) | (static_cast<uint32_t>(data[3]) << 24);
}

}  // namespace

Uf2ValidationInfo Uf2Validator::validate(const String& path)
{
  Uf2ValidationInfo info;

  if (!SD.exists(path)) {
    info.result = Uf2ValidationResult::FileMissing;
    return info;
  }

  File file = SD.open(path, FILE_READ);
  if (!file) {
    info.result = Uf2ValidationResult::OpenFailed;
    return info;
  }

  info.fileSize = file.size();
  if (info.fileSize == 0) {
    file.close();
    info.result = Uf2ValidationResult::EmptyFile;
    return info;
  }
  if ((info.fileSize % 512) != 0) {
    file.close();
    info.result = Uf2ValidationResult::SizeNotAligned;
    return info;
  }

  uint8_t block[512];
  size_t readBytes = file.read(block, sizeof(block));
  file.close();
  if (readBytes != sizeof(block)) {
    info.result = Uf2ValidationResult::ReadFailed;
    return info;
  }

  if (readLe32(block) != UF2_MAGIC_START0 || readLe32(block + 4) != UF2_MAGIC_START1 ||
      readLe32(block + 508) != UF2_MAGIC_END) {
    info.result = Uf2ValidationResult::InvalidMagic;
    return info;
  }

  const uint32_t flags = readLe32(block + 8);
  if ((flags & UF2_FLAG_FAMILY_ID_PRESENT) != 0) {
    info.familyId = readLe32(block + 28);
    info.hasFamilyId = true;
  }
  // TODO: Add RP2040/RP2350 family-id warnings once project target mapping is finalized.
  info.result = Uf2ValidationResult::Ok;
  return info;
}

const char* Uf2Validator::message(Uf2ValidationResult result)
{
  switch (result) {
    case Uf2ValidationResult::Ok:
      return "UF2 OK";
    case Uf2ValidationResult::FileMissing:
      return "File missing";
    case Uf2ValidationResult::EmptyFile:
      return "Empty UF2";
    case Uf2ValidationResult::SizeNotAligned:
      return "Invalid UF2 size";
    case Uf2ValidationResult::OpenFailed:
      return "Open UF2 failed";
    case Uf2ValidationResult::ReadFailed:
      return "Read UF2 failed";
    case Uf2ValidationResult::InvalidMagic:
      return "Invalid UF2";
  }
  return "Invalid UF2";
}
