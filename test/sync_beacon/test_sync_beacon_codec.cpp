#include <cassert>
#include <cstdint>
#include <cstring>

#include "SyncBeaconCodec.h"

int main()
{
  const uint8_t check[] = "123456789";
  assert(NightKiteSync::crc16Ccitt(check, 9) == 0x29B1);

  NightKiteSync::BeaconInput input;
  input.group = 4;
  input.flags = NightKiteSync::FLAG_AUDIO_BEAT;
  input.sequence = 0x1234;
  input.pattern = 27;
  input.brightness = 159;
  input.phaseMs = 0x78563412;
  input.beatMs = 500;

  uint8_t data[31] = {};
  auto encoded = NightKiteSync::encodeAdvertising(input, data, sizeof(data));
  assert(encoded.size == NightKiteSync::V1_ADVERTISING_SIZE);
  assert(data[0] == 2 && data[1] == 0x01 && data[2] == 0x06);
  assert(data[3] == 20 && data[4] == 0xFF && data[5] == 0xFF && data[6] == 0xFF);
  assert(std::memcmp(data + 7, "NK", 2) == 0);
  assert(data[9] == NightKiteSync::VERSION_V1 && data[10] == 4 && data[11] == 1);
  assert(data[12] == 0x34 && data[13] == 0x12 && data[14] == 27 && data[15] == 159);
  assert(data[16] == 0x12 && data[17] == 0x34 && data[18] == 0x56 && data[19] == 0x78);
  assert(NightKiteSync::validateAdvertising(data, encoded.size));

  input.version = NightKiteSync::VERSION_V2;
  input.flags = NightKiteSync::FLAG_AUDIO_BEAT | NightKiteSync::FLAG_AUDIO_SIGNAL_VALID |
                NightKiteSync::FLAG_AUDIO_BEAT_LOCKED;
  input.energy = 11;
  input.bass = 22;
  input.mid = 33;
  input.treble = 44;
  input.confidence = 55;
  encoded = NightKiteSync::encodeAdvertising(input, data, sizeof(data));
  assert(encoded.size == NightKiteSync::V2_ADVERTISING_SIZE);
  assert(NightKiteSync::V2_PACKET_SIZE == 22 && NightKiteSync::V2_ADVERTISING_SIZE == 29);
  assert(data[3] == 25 && data[9] == NightKiteSync::VERSION_V2);
  assert(data[11] == (NightKiteSync::FLAG_AUDIO_BEAT | NightKiteSync::FLAG_AUDIO_SIGNAL_VALID |
                      NightKiteSync::FLAG_AUDIO_BEAT_LOCKED));
  assert(data[22] == 11 && data[23] == 22 && data[24] == 33 && data[25] == 44 && data[26] == 55);
  assert(NightKiteSync::validateAdvertising(data, encoded.size));
  data[24] ^= 1;
  assert(!NightKiteSync::validateAdvertising(data, encoded.size));

  assert(NightKiteSync::encodeAdvertising(input, data, NightKiteSync::V2_ADVERTISING_SIZE - 1).size == 0);
}
