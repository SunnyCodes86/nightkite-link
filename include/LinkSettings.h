#pragma once

#include <cstddef>
#include <cstdint>

#ifndef NIGHTKITE_SOUND_ENABLED
#define NIGHTKITE_SOUND_ENABLED 1
#endif

namespace NightKiteLinkSettings {

constexpr uint32_t MAGIC = 0x4E4B4C53UL;
constexpr uint8_t VERSION = 1;
constexpr uint8_t FLAG_SOUND = 1 << 0;
constexpr uint8_t FLAG_KEY_SOUNDS = 1 << 1;
constexpr uint8_t FLAG_STARTUP_SOUND = 1 << 2;
constexpr uint8_t VALID_FLAGS = FLAG_SOUND | FLAG_KEY_SOUNDS | FLAG_STARTUP_SOUND;

constexpr uint8_t VOLUME_LEVELS[] = {60, 90, 120, 150, 180, 210, 240, 255};
constexpr uint8_t DISPLAY_BRIGHTNESS_LEVELS[] = {32, 64, 96, 128, 160, 192, 224, 255};

struct Settings {
  bool soundEnabled = NIGHTKITE_SOUND_ENABLED != 0;
  uint8_t volume = 210;
  bool keySoundsEnabled = true;
  bool startupSoundEnabled = true;
  uint8_t displayBrightness = 96;
};

struct __attribute__((packed)) Record {
  uint32_t magic;
  uint8_t version;
  uint8_t flags;
  uint8_t volume;
  uint8_t displayBrightness;
  uint8_t checksum;
};

static_assert(sizeof(Record) == 9, "Link settings record layout changed");

inline bool contains(const uint8_t* values, std::size_t count, uint8_t value)
{
  for (std::size_t i = 0; i < count; ++i) {
    if (values[i] == value) return true;
  }
  return false;
}

inline uint8_t checksum(const Record& record)
{
  return static_cast<uint8_t>(record.magic ^ (record.magic >> 8) ^ (record.magic >> 16) ^
                              (record.magic >> 24) ^ record.version ^ record.flags ^ record.volume ^
                              record.displayBrightness ^ 0xA5);
}

inline Record encode(const Settings& settings)
{
  Record record{MAGIC, VERSION, 0, settings.volume, settings.displayBrightness, 0};
  if (settings.soundEnabled) record.flags |= FLAG_SOUND;
  if (settings.keySoundsEnabled) record.flags |= FLAG_KEY_SOUNDS;
  if (settings.startupSoundEnabled) record.flags |= FLAG_STARTUP_SOUND;
  record.checksum = checksum(record);
  return record;
}

inline bool decode(const Record& record, Settings& settings)
{
  if (record.magic != MAGIC || record.version != VERSION || (record.flags & ~VALID_FLAGS) != 0 ||
      record.checksum != checksum(record) ||
      !contains(VOLUME_LEVELS, sizeof(VOLUME_LEVELS), record.volume) ||
      !contains(DISPLAY_BRIGHTNESS_LEVELS, sizeof(DISPLAY_BRIGHTNESS_LEVELS), record.displayBrightness)) {
    return false;
  }
  settings.soundEnabled = (record.flags & FLAG_SOUND) != 0;
  settings.volume = record.volume;
  settings.keySoundsEnabled = (record.flags & FLAG_KEY_SOUNDS) != 0;
  settings.startupSoundEnabled = (record.flags & FLAG_STARTUP_SOUND) != 0;
  settings.displayBrightness = record.displayBrightness;
  return true;
}

inline bool equal(const Settings& left, const Settings& right)
{
  return left.soundEnabled == right.soundEnabled && left.volume == right.volume &&
         left.keySoundsEnabled == right.keySoundsEnabled &&
         left.startupSoundEnabled == right.startupSoundEnabled &&
         left.displayBrightness == right.displayBrightness;
}

}  // namespace NightKiteLinkSettings
