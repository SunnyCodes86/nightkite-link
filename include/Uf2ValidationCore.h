#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

enum class Uf2Target : uint8_t {
  Rp2040,
  Rp2350,
};

enum class Uf2ValidationResult {
  Ok,
  FileMissing,
  EmptyFile,
  SizeNotAligned,
  FileTooLarge,
  OpenFailed,
  ReadFailed,
  InvalidMagic,
  InvalidBlock,
  MissingFamilyId,
  TargetMismatch,
  DuplicateBlock,
  IncompleteFile,
};

class Uf2ValidationCore {
public:
  Uf2ValidationCore(Uf2Target target, size_t blockCount);

  Uf2ValidationResult addBlock(const uint8_t* block, size_t size);
  Uf2ValidationResult finish() const;
  uint32_t familyId() const;

private:
  Uf2Target target;
  size_t blockCount;
  size_t seenCount = 0;
  uint32_t detectedFamilyId = 0;
  bool hasFamilyId = false;
  Uf2ValidationResult result = Uf2ValidationResult::Ok;
  std::vector<uint8_t> seenBlocks;
};

bool uf2FamilyMatchesTarget(uint32_t familyId, Uf2Target target);
bool uf2BootselDeviceMatchesTarget(uint16_t vendorId, uint16_t productId, Uf2Target target);
