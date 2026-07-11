#include "Uf2ValidationCore.h"

namespace {

constexpr size_t UF2_BLOCK_SIZE = 512;
constexpr uint32_t UF2_MAGIC_START0 = 0x0A324655;
constexpr uint32_t UF2_MAGIC_START1 = 0x9E5D5157;
constexpr uint32_t UF2_MAGIC_END = 0x0AB16F30;
constexpr uint32_t UF2_FLAG_FAMILY_ID_PRESENT = 0x00002000;
constexpr uint32_t RP2040_FAMILY_ID = 0xE48BFF56;
constexpr uint32_t RP2350_ARM_S_FAMILY_ID = 0xE48BFF59;
constexpr uint32_t RP2350_RISCV_FAMILY_ID = 0xE48BFF5A;
constexpr uint32_t RP2350_ARM_NS_FAMILY_ID = 0xE48BFF5B;
constexpr uint16_t RASPBERRY_PI_USB_VID = 0x2E8A;
constexpr uint16_t RP2040_BOOTSEL_PID = 0x0003;
constexpr uint16_t RP2350_BOOTSEL_PID = 0x000F;

uint32_t readLe32(const uint8_t* data)
{
  return static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8) |
         (static_cast<uint32_t>(data[2]) << 16) | (static_cast<uint32_t>(data[3]) << 24);
}

}  // namespace

Uf2ValidationCore::Uf2ValidationCore(Uf2Target expectedTarget, size_t expectedBlockCount)
    : target(expectedTarget), blockCount(expectedBlockCount), seenBlocks((expectedBlockCount + 7) / 8, 0)
{
}

Uf2ValidationResult Uf2ValidationCore::addBlock(const uint8_t* block, size_t size)
{
  if (result != Uf2ValidationResult::Ok) {
    return result;
  }
  if (block == nullptr || size != UF2_BLOCK_SIZE || readLe32(block) != UF2_MAGIC_START0 ||
      readLe32(block + 4) != UF2_MAGIC_START1 || readLe32(block + 508) != UF2_MAGIC_END) {
    return result = Uf2ValidationResult::InvalidMagic;
  }

  const uint32_t payloadSize = readLe32(block + 16);
  const uint32_t blockNumber = readLe32(block + 20);
  const uint32_t declaredBlockCount = readLe32(block + 24);
  if (payloadSize > 476 || declaredBlockCount != blockCount || blockNumber >= blockCount) {
    return result = Uf2ValidationResult::InvalidBlock;
  }

  const size_t byteIndex = blockNumber / 8;
  const uint8_t bit = static_cast<uint8_t>(1U << (blockNumber % 8));
  if ((seenBlocks[byteIndex] & bit) != 0) {
    return result = Uf2ValidationResult::DuplicateBlock;
  }
  seenBlocks[byteIndex] |= bit;
  ++seenCount;

  const uint32_t flags = readLe32(block + 8);
  if ((flags & UF2_FLAG_FAMILY_ID_PRESENT) != 0) {
    const uint32_t familyId = readLe32(block + 28);
    if (!uf2FamilyMatchesTarget(familyId, target)) {
      return result = Uf2ValidationResult::TargetMismatch;
    }
    if (!hasFamilyId) {
      detectedFamilyId = familyId;
      hasFamilyId = true;
    }
  }
  return result;
}

Uf2ValidationResult Uf2ValidationCore::finish() const
{
  if (result != Uf2ValidationResult::Ok) {
    return result;
  }
  if (!hasFamilyId) {
    return Uf2ValidationResult::MissingFamilyId;
  }
  return seenCount == blockCount ? Uf2ValidationResult::Ok : Uf2ValidationResult::IncompleteFile;
}

uint32_t Uf2ValidationCore::familyId() const
{
  return detectedFamilyId;
}

bool uf2FamilyMatchesTarget(uint32_t familyId, Uf2Target target)
{
  if (target == Uf2Target::Rp2040) {
    return familyId == RP2040_FAMILY_ID;
  }
  return familyId == RP2350_ARM_S_FAMILY_ID || familyId == RP2350_RISCV_FAMILY_ID ||
         familyId == RP2350_ARM_NS_FAMILY_ID;
}

bool uf2BootselDeviceMatchesTarget(uint16_t vendorId, uint16_t productId, Uf2Target target)
{
  return vendorId == RASPBERRY_PI_USB_VID &&
         productId == (target == Uf2Target::Rp2040 ? RP2040_BOOTSEL_PID : RP2350_BOOTSEL_PID);
}
