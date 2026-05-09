#pragma once

#include <Arduino.h>

namespace NightKiteSoundAssets {

struct SoundStep {
  uint16_t startMs;
  uint16_t durationMs;
  float frequencyHz;
  float endFrequencyHz;
  float harmonicHz;
  float gain;
};

struct SoundClip {
  const SoundStep* steps;
  size_t stepCount;
  uint16_t durationMs;
  float gain;
};

constexpr uint32_t SAMPLE_RATE = 16000;
constexpr uint16_t MAX_DURATION_MS = 560;
constexpr size_t MAX_PCM_SAMPLES = (SAMPLE_RATE * MAX_DURATION_MS) / 1000;
constexpr size_t KEY_VARIANT_COUNT = 5;
constexpr size_t NAVIGATE_VARIANT_COUNT = 2;

extern const SoundClip startupClip;
extern const SoundClip keyClip;
extern const SoundClip keyClips[KEY_VARIANT_COUNT];
extern const SoundClip navigateClip;
extern const SoundClip navigateClips[NAVIGATE_VARIANT_COUNT];
extern const SoundClip pageChangeClip;
extern const SoundClip transferCompleteClip;
extern const SoundClip confirmClip;
extern const SoundClip cancelClip;
extern const SoundClip successClip;
extern const SoundClip errorClip;

}  // namespace NightKiteSoundAssets
