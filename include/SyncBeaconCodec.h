#pragma once

#include <cstddef>
#include <cstdint>

namespace NightKiteSync {

constexpr uint8_t VERSION_V1 = 1;
constexpr uint8_t VERSION_V2 = 2;
constexpr uint8_t FLAG_AUDIO_BEAT = 0x01;
constexpr uint8_t FLAG_AUDIO_SIGNAL_VALID = 0x02;
constexpr uint8_t FLAG_AUDIO_BEAT_LOCKED = 0x04;
constexpr size_t V1_PACKET_SIZE = 17;
constexpr size_t V2_PACKET_SIZE = 22;
constexpr size_t V1_ADVERTISING_SIZE = 24;
constexpr size_t V2_ADVERTISING_SIZE = 29;

struct BeaconInput {
  uint8_t version = VERSION_V1;
  uint8_t group = 1;
  uint8_t flags = 0;
  uint16_t sequence = 0;
  uint8_t pattern = 1;
  uint8_t brightness = 159;
  uint32_t phaseMs = 0;
  uint16_t beatMs = 500;
  uint8_t energy = 0;
  uint8_t bass = 0;
  uint8_t mid = 0;
  uint8_t treble = 0;
  uint8_t confidence = 0;
};

struct EncodeResult {
  size_t size;
  uint16_t crc;

  EncodeResult(size_t encodedSize = 0, uint16_t encodedCrc = 0) : size(encodedSize), crc(encodedCrc) {}
};

uint16_t crc16Ccitt(const uint8_t* data, size_t size);
EncodeResult encodeAdvertising(const BeaconInput& input, uint8_t* output, size_t capacity);
bool validateAdvertising(const uint8_t* data, size_t size);

}  // namespace NightKiteSync
