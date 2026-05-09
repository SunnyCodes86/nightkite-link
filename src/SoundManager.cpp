#include "SoundManager.h"

#include <math.h>
#include "M5Cardputer.h"
#include "SoundAssets.h"

#ifndef NIGHTKITE_SOUND_ENABLED
#define NIGHTKITE_SOUND_ENABLED 1
#endif

namespace {

int16_t pcmBuffers[2][NightKiteSoundAssets::MAX_PCM_SAMPLES];
constexpr float PI2 = 6.28318530718f;
constexpr uint8_t DEFAULT_VOLUME = 210;
constexpr float PCM_SCALE = 22000.0f;
constexpr int PCM_LIMIT = 27000;

float envelope(size_t sample, size_t total)
{
  if (total == 0) {
    return 0.0f;
  }
  const size_t attack = min<size_t>(total / 5, NightKiteSoundAssets::SAMPLE_RATE / 180);
  if (attack > 0 && sample < attack) {
    return static_cast<float>(sample) / static_cast<float>(attack);
  }
  float t = static_cast<float>(sample) / static_cast<float>(total);
  float decay = 1.0f - t;
  return decay * decay;
}

size_t renderClip(const NightKiteSoundAssets::SoundClip& clip, int16_t* out, size_t capacity)
{
  size_t totalSamples = min<size_t>((NightKiteSoundAssets::SAMPLE_RATE * clip.durationMs) / 1000, capacity);
  for (size_t i = 0; i < totalSamples; ++i) {
    out[i] = 0;
  }

  for (size_t stepIndex = 0; stepIndex < clip.stepCount; ++stepIndex) {
    const auto& step = clip.steps[stepIndex];
    size_t start = (NightKiteSoundAssets::SAMPLE_RATE * step.startMs) / 1000;
    size_t count = (NightKiteSoundAssets::SAMPLE_RATE * step.durationMs) / 1000;
    if (start >= totalSamples) {
      continue;
    }
    count = min(count, totalSamples - start);
    for (size_t i = 0; i < count; ++i) {
      float progress = count > 1 ? static_cast<float>(i) / static_cast<float>(count - 1) : 0.0f;
      float frequency = step.frequencyHz + (step.endFrequencyHz - step.frequencyHz) * progress;
      float t = static_cast<float>(i) / static_cast<float>(NightKiteSoundAssets::SAMPLE_RATE);
      float env = envelope(i, count);
      float wave = sinf(PI2 * frequency * t);
      wave += 0.18f * sinf(PI2 * step.harmonicHz * t);
      float value = wave * env * step.gain * clip.gain * PCM_SCALE;
      int mixed = static_cast<int>(out[start + i]) + static_cast<int>(value);
      out[start + i] = static_cast<int16_t>(constrain(mixed, -PCM_LIMIT, PCM_LIMIT));
    }
  }
  return totalSamples;
}

}  // namespace

void SoundManager::begin()
{
  enabled = NIGHTKITE_SOUND_ENABLED != 0;
  currentVolume = DEFAULT_VOLUME;
  M5Cardputer.Speaker.begin();
  M5Cardputer.Speaker.setVolume(currentVolume);
  if (!enabled) {
    Serial.println("WAV/PCM sound disabled");
  }
}

void SoundManager::setEnabled(bool value)
{
  enabled = value;
  if (!enabled) {
    M5Cardputer.Speaker.stop();
  }
}

bool SoundManager::isEnabled() const
{
  return enabled;
}

void SoundManager::setVolume(uint8_t value)
{
  currentVolume = value;
  M5Cardputer.Speaker.setVolume(currentVolume);
}

uint8_t SoundManager::volume() const
{
  return currentVolume;
}

void SoundManager::playStartup()
{
  playClip(NightKiteSoundAssets::startupClip, true);
}

void SoundManager::playKey()
{
  playTextKey();
}

void SoundManager::playTextKey()
{
  if (shouldPlayUiClick()) {
    uint8_t variant = chooseNextKeyVariant();
    const auto& clip = NightKiteSoundAssets::keyClips[variant];
    playClip(clip, true);
  }
}

void SoundManager::playPageChange()
{
  if (shouldPlayUiClick()) {
    playClip(NightKiteSoundAssets::pageChangeClip, true);
  }
}

void SoundManager::playTransferComplete()
{
  playClip(NightKiteSoundAssets::transferCompleteClip, true);
}

void SoundManager::playNavigate()
{
  if (shouldPlayUiClick()) {
    const auto& clip =
        NightKiteSoundAssets::navigateClips[navigateVariantIndex % NightKiteSoundAssets::NAVIGATE_VARIANT_COUNT];
    navigateVariantIndex = (navigateVariantIndex + 1) % NightKiteSoundAssets::NAVIGATE_VARIANT_COUNT;
    playClip(clip, true);
  }
}

void SoundManager::playConfirm()
{
  playClip(NightKiteSoundAssets::confirmClip, true);
}

void SoundManager::playCancel()
{
  playClip(NightKiteSoundAssets::cancelClip, true);
}

void SoundManager::playSuccess()
{
  playClip(NightKiteSoundAssets::successClip, true);
}

void SoundManager::playError()
{
  playClip(NightKiteSoundAssets::errorClip, true);
}

void SoundManager::update()
{
}

bool SoundManager::isPlaying() const
{
  return M5Cardputer.Speaker.isPlaying();
}

uint8_t SoundManager::chooseNextKeyVariant()
{
  if (keyVariantBagIndex >= NightKiteSoundAssets::KEY_VARIANT_COUNT) {
    refillKeyVariantBag();
  }
  uint8_t variant = keyVariantBag[keyVariantBagIndex++];
  lastKeyVariant = variant;
  return variant;
}

void SoundManager::refillKeyVariantBag()
{
  for (uint8_t i = 0; i < NightKiteSoundAssets::KEY_VARIANT_COUNT; ++i) {
    keyVariantBag[i] = i;
  }
  for (int i = NightKiteSoundAssets::KEY_VARIANT_COUNT - 1; i > 0; --i) {
    int j = random(i + 1);
    uint8_t tmp = keyVariantBag[i];
    keyVariantBag[i] = keyVariantBag[j];
    keyVariantBag[j] = tmp;
  }
  if (keyVariantBag[0] == lastKeyVariant) {
    uint8_t tmp = keyVariantBag[0];
    keyVariantBag[0] = keyVariantBag[1];
    keyVariantBag[1] = tmp;
  }
  keyVariantBagIndex = 0;
}

void SoundManager::playClip(const NightKiteSoundAssets::SoundClip& clip, bool interrupt)
{
  if (!enabled) {
    return;
  }
  M5Cardputer.Speaker.setVolume(currentVolume);
  if (interrupt) {
    M5Cardputer.Speaker.stop();
  }
  bufferIndex = (bufferIndex + 1) % 2;
  int16_t* buffer = pcmBuffers[bufferIndex];
  size_t samples = renderClip(clip, buffer, NightKiteSoundAssets::MAX_PCM_SAMPLES);
  if (samples == 0 ||
      !M5Cardputer.Speaker.playRaw(buffer, samples, NightKiteSoundAssets::SAMPLE_RATE, false, 1, -1, interrupt)) {
    Serial.println("WAV sound backend unavailable");
  }
}

bool SoundManager::shouldPlayUiClick()
{
  unsigned long now = millis();
  if (now - lastClickMs < 38) {
    return false;
  }
  lastClickMs = now;
  return true;
}
