#include "Uf2Validator.h"

namespace {

constexpr size_t UF2_BLOCK_SIZE = 512;
constexpr size_t MAX_UF2_FILE_SIZE = 32 * 1024 * 1024;

}  // namespace

Uf2ValidationInfo Uf2Validator::validate(const String& path, Uf2Target target)
{
  return validate(SD, path, target);
}

Uf2ValidationInfo Uf2Validator::validate(fs::FS& storage, const String& path, Uf2Target target)
{
  Uf2ValidationInfo info;

  if (!storage.exists(path)) {
    info.result = Uf2ValidationResult::FileMissing;
    return info;
  }

  File file = storage.open(path, FILE_READ);
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
  if ((info.fileSize % UF2_BLOCK_SIZE) != 0) {
    file.close();
    info.result = Uf2ValidationResult::SizeNotAligned;
    return info;
  }
  if (info.fileSize > MAX_UF2_FILE_SIZE) {
    file.close();
    info.result = Uf2ValidationResult::FileTooLarge;
    return info;
  }

  const size_t blockCount = info.fileSize / UF2_BLOCK_SIZE;
  Uf2ValidationCore validator(target, blockCount);
  uint8_t block[UF2_BLOCK_SIZE];
  for (size_t index = 0; index < blockCount; ++index) {
    if (file.read(block, sizeof(block)) != sizeof(block)) {
      file.close();
      info.result = Uf2ValidationResult::ReadFailed;
      return info;
    }
    info.result = validator.addBlock(block, sizeof(block));
    if (info.result != Uf2ValidationResult::Ok) {
      file.close();
      return info;
    }
  }
  file.close();
  info.result = validator.finish();
  info.familyId = validator.familyId();
  info.hasFamilyId = info.familyId != 0;
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
    case Uf2ValidationResult::FileTooLarge:
      return "UF2 too large";
    case Uf2ValidationResult::OpenFailed:
      return "Open UF2 failed";
    case Uf2ValidationResult::ReadFailed:
      return "Read UF2 failed";
    case Uf2ValidationResult::InvalidMagic:
      return "Invalid UF2";
    case Uf2ValidationResult::InvalidBlock:
    case Uf2ValidationResult::DuplicateBlock:
    case Uf2ValidationResult::IncompleteFile:
      return "Invalid UF2 blocks";
    case Uf2ValidationResult::MissingFamilyId:
      return "UF2 family missing";
    case Uf2ValidationResult::TargetMismatch:
      return "UF2 target mismatch";
  }
  return "Invalid UF2";
}
