#include "SyncBeaconCodec.h"

namespace NightKiteSync {
namespace {

constexpr uint8_t AD_TYPE_FLAGS = 0x01;
constexpr uint8_t AD_TYPE_MANUFACTURER = 0xFF;
constexpr uint16_t COMPANY_ID = 0xFFFF;

void put16(uint8_t* output, size_t offset, uint16_t value)
{
  output[offset] = static_cast<uint8_t>(value);
  output[offset + 1] = static_cast<uint8_t>(value >> 8);
}

void put32(uint8_t* output, size_t offset, uint32_t value)
{
  put16(output, offset, static_cast<uint16_t>(value));
  put16(output, offset + 2, static_cast<uint16_t>(value >> 16));
}

uint16_t get16(const uint8_t* input, size_t offset)
{
  return static_cast<uint16_t>(input[offset]) | (static_cast<uint16_t>(input[offset + 1]) << 8);
}

}  // namespace

uint16_t crc16Ccitt(const uint8_t* data, size_t size)
{
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < size; ++i) {
    crc ^= static_cast<uint16_t>(data[i]) << 8;
    for (uint8_t bit = 0; bit < 8; ++bit) {
      crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021) : static_cast<uint16_t>(crc << 1);
    }
  }
  return crc;
}

EncodeResult encodeAdvertising(const BeaconInput& input, uint8_t* output, size_t capacity)
{
  const bool v2 = input.version == VERSION_V2;
  const size_t packetSize = v2 ? V2_PACKET_SIZE : V1_PACKET_SIZE;
  const size_t advertisingSize = v2 ? V2_ADVERTISING_SIZE : V1_ADVERTISING_SIZE;
  if (output == nullptr || capacity < advertisingSize || (input.version != VERSION_V1 && !v2)) {
    return {};
  }

  output[0] = 2;
  output[1] = AD_TYPE_FLAGS;
  output[2] = 0x06;
  output[3] = static_cast<uint8_t>(packetSize + 3);
  output[4] = AD_TYPE_MANUFACTURER;
  put16(output, 5, COMPANY_ID);

  uint8_t* packet = output + 7;
  packet[0] = 'N';
  packet[1] = 'K';
  packet[2] = input.version;
  packet[3] = input.group;
  packet[4] = input.flags;
  put16(packet, 5, input.sequence);
  packet[7] = input.pattern;
  packet[8] = input.brightness;
  put32(packet, 9, input.phaseMs);
  put16(packet, 13, input.beatMs);
  if (v2) {
    packet[15] = input.energy;
    packet[16] = input.bass;
    packet[17] = input.mid;
    packet[18] = input.treble;
    packet[19] = input.confidence;
  }
  put16(packet, packetSize - 2, 0);
  const uint16_t crc = crc16Ccitt(packet, packetSize);
  put16(packet, packetSize - 2, crc);
  return {advertisingSize, crc};
}

bool validateAdvertising(const uint8_t* data, size_t size)
{
  if (data == nullptr || (size != V1_ADVERTISING_SIZE && size != V2_ADVERTISING_SIZE) || data[0] != 2 ||
      data[1] != AD_TYPE_FLAGS || data[2] != 0x06 || data[4] != AD_TYPE_MANUFACTURER ||
      get16(data, 5) != COMPANY_ID) {
    return false;
  }
  const uint8_t* packet = data + 7;
  const size_t packetSize = size - 7;
  if (data[3] != packetSize + 3 || packet[0] != 'N' || packet[1] != 'K' ||
      (packet[2] == VERSION_V1 ? packetSize != V1_PACKET_SIZE
                              : packet[2] == VERSION_V2 ? packetSize != V2_PACKET_SIZE : true)) {
    return false;
  }
  uint8_t copy[V2_PACKET_SIZE] = {};
  for (size_t i = 0; i < packetSize; ++i) {
    copy[i] = packet[i];
  }
  const uint16_t expected = get16(copy, packetSize - 2);
  put16(copy, packetSize - 2, 0);
  return crc16Ccitt(copy, packetSize) == expected;
}

}  // namespace NightKiteSync
