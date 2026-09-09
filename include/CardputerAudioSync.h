#pragma once

#include <Arduino.h>

#include "AudioSyncDsp.h"

class SoundManager;

class CardputerAudioSync {
public:
  static constexpr uint32_t SAMPLE_RATE = 8000;
  static constexpr size_t FRAME_SAMPLES = 256;

  bool begin(SoundManager& sound);
  void end();
  void tick(const AudioSyncDspConfig& config);

  bool active() const;
  bool failed() const;
  AudioSyncDspOutput output(uint32_t nowMs, const AudioSyncDspConfig& config) const;
  unsigned long frameCount() const;

private:
  bool queueCurrentFrame();
  void fail(const char* reason);
  void printPeriodicDebug(const AudioSyncDspConfig& config, uint32_t nowMs);

  SoundManager* soundManager = nullptr;
  AudioSyncDsp dsp;
  int16_t frames[2][FRAME_SAMPLES] = {};
  uint8_t currentFrame = 0;
  bool frameQueued = false;
  bool running = false;
  bool error = false;
  bool previousBeat = false;
  unsigned long framesProcessed = 0;
  uint32_t lastDebugMs = 0;
};
