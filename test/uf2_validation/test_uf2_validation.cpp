#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "Uf2ValidationCore.h"

namespace {

constexpr size_t BLOCK_SIZE = 512;
constexpr uint32_t FAMILY_FLAG = 0x00002000;
constexpr uint32_t RP2040_FAMILY = 0xE48BFF56;
constexpr uint32_t RP2350_ARM_S_FAMILY = 0xE48BFF59;

void writeLe32(uint8_t* output, uint32_t value)
{
  output[0] = static_cast<uint8_t>(value);
  output[1] = static_cast<uint8_t>(value >> 8);
  output[2] = static_cast<uint8_t>(value >> 16);
  output[3] = static_cast<uint8_t>(value >> 24);
}

void makeBlock(uint8_t* block, uint32_t blockNumber, uint32_t blockCount, uint32_t familyId)
{
  memset(block, 0, BLOCK_SIZE);
  writeLe32(block, 0x0A324655);
  writeLe32(block + 4, 0x9E5D5157);
  writeLe32(block + 8, FAMILY_FLAG);
  writeLe32(block + 12, 0x10000000 + blockNumber * 256);
  writeLe32(block + 16, 256);
  writeLe32(block + 20, blockNumber);
  writeLe32(block + 24, blockCount);
  writeLe32(block + 28, familyId);
  writeLe32(block + 508, 0x0AB16F30);
}

Uf2ValidationResult validateTwoBlocks(Uf2Target target, uint32_t familyId)
{
  Uf2ValidationCore validator(target, 2);
  uint8_t block[BLOCK_SIZE];
  makeBlock(block, 0, 2, familyId);
  assert(validator.addBlock(block, sizeof(block)) == Uf2ValidationResult::Ok);
  makeBlock(block, 1, 2, familyId);
  assert(validator.addBlock(block, sizeof(block)) == Uf2ValidationResult::Ok);
  return validator.finish();
}

void testValidTargetsAndBootselDevices()
{
  assert(validateTwoBlocks(Uf2Target::Rp2040, RP2040_FAMILY) == Uf2ValidationResult::Ok);
  assert(validateTwoBlocks(Uf2Target::Rp2350, RP2350_ARM_S_FAMILY) == Uf2ValidationResult::Ok);
  assert(uf2FamilyMatchesTarget(0xE48BFF5A, Uf2Target::Rp2350));
  assert(uf2FamilyMatchesTarget(0xE48BFF5B, Uf2Target::Rp2350));
  assert(uf2BootselDeviceMatchesTarget(0x2E8A, 0x0003, Uf2Target::Rp2040));
  assert(uf2BootselDeviceMatchesTarget(0x2E8A, 0x000F, Uf2Target::Rp2350));
  assert(!uf2BootselDeviceMatchesTarget(0x2E8A, 0x000F, Uf2Target::Rp2040));
  assert(!uf2BootselDeviceMatchesTarget(0x1234, 0x0003, Uf2Target::Rp2040));
}

void testTargetAndFamilyFailures()
{
  Uf2ValidationCore wrongTarget(Uf2Target::Rp2040, 1);
  uint8_t block[BLOCK_SIZE];
  makeBlock(block, 0, 1, RP2350_ARM_S_FAMILY);
  assert(wrongTarget.addBlock(block, sizeof(block)) == Uf2ValidationResult::TargetMismatch);

  Uf2ValidationCore missingFamily(Uf2Target::Rp2040, 1);
  makeBlock(block, 0, 1, RP2040_FAMILY);
  writeLe32(block + 8, 0);
  assert(missingFamily.addBlock(block, sizeof(block)) == Uf2ValidationResult::Ok);
  assert(missingFamily.finish() == Uf2ValidationResult::MissingFamilyId);
}

void testCorruptAndIncompleteBlocks()
{
  uint8_t block[BLOCK_SIZE];

  Uf2ValidationCore badMagic(Uf2Target::Rp2040, 1);
  makeBlock(block, 0, 1, RP2040_FAMILY);
  block[508] = 0;
  assert(badMagic.addBlock(block, sizeof(block)) == Uf2ValidationResult::InvalidMagic);

  Uf2ValidationCore badPayload(Uf2Target::Rp2040, 1);
  makeBlock(block, 0, 1, RP2040_FAMILY);
  writeLe32(block + 16, 477);
  assert(badPayload.addBlock(block, sizeof(block)) == Uf2ValidationResult::InvalidBlock);

  Uf2ValidationCore badCount(Uf2Target::Rp2040, 2);
  makeBlock(block, 0, 3, RP2040_FAMILY);
  assert(badCount.addBlock(block, sizeof(block)) == Uf2ValidationResult::InvalidBlock);

  Uf2ValidationCore duplicate(Uf2Target::Rp2040, 2);
  makeBlock(block, 0, 2, RP2040_FAMILY);
  assert(duplicate.addBlock(block, sizeof(block)) == Uf2ValidationResult::Ok);
  assert(duplicate.addBlock(block, sizeof(block)) == Uf2ValidationResult::DuplicateBlock);

  Uf2ValidationCore incomplete(Uf2Target::Rp2040, 2);
  makeBlock(block, 0, 2, RP2040_FAMILY);
  assert(incomplete.addBlock(block, sizeof(block)) == Uf2ValidationResult::Ok);
  assert(incomplete.finish() == Uf2ValidationResult::IncompleteFile);
}

}  // namespace

int main()
{
  testValidTargetsAndBootselDevices();
  testTargetAndFamilyFailures();
  testCorruptAndIncompleteBlocks();
  return 0;
}
