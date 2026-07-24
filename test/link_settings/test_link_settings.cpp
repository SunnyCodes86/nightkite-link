#include <assert.h>

#include "LinkSettings.h"

using namespace NightKiteLinkSettings;

int main()
{
  Settings defaults;
  Settings decoded;
  Record record = encode(defaults);
  assert(decode(record, decoded));
  assert(equal(defaults, decoded));

  Settings custom;
  custom.soundEnabled = false;
  custom.volume = 150;
  custom.keySoundsEnabled = false;
  custom.startupSoundEnabled = false;
  custom.displayBrightness = 224;
  record = encode(custom);
  assert(decode(record, decoded));
  assert(equal(custom, decoded));

  record.checksum ^= 1;
  assert(!decode(record, decoded));
  record = encode(defaults);
  record.version++;
  record.checksum = checksum(record);
  assert(!decode(record, decoded));
  record = encode(defaults);
  record.volume = 211;
  record.checksum = checksum(record);
  assert(!decode(record, decoded));
  record = encode(defaults);
  record.flags |= 0x80;
  record.checksum = checksum(record);
  assert(!decode(record, decoded));
  return 0;
}
