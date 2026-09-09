#pragma once

#include <Arduino.h>

#include "AudioSyncDsp.h"

class SoundManager;

class CardputerAudioSync {
public:
  static constexpr uint32_t SAMPLE_RATE = 16000;
  static constexpr size_t FRAME_SAMPLES = AudioSyncDsp::HOP_SAMPLES;

  bool begin(SoundManager& sound);
  void end();
  void tick(const AudioSyncDspConfig& config);

  bool active() const;
  bool failed() const;
  AudioSyncDspOutput output(uint32_t nowMs, const AudioSyncDspConfig& config) const;
  unsigned long frameCount() const;
  unsigned long captureDropCount() const;

private:
  bool queueFrame(uint8_t frame);
  void fail(const char* reason);

  SoundManager* soundManager = nullptr;
  AudioSyncDsp dsp;
  int16_t frames[2][FRAME_SAMPLES] = {};
  uint8_t currentFrame = 0;
  bool frameQueued = false;
  bool running = false;
  bool error = false;
  unsigned long framesProcessed = 0;
  unsigned long captureDrops = 0;
  uint32_t captureStartedMs = 0;
  uint32_t nextCaptureCheckMs = 0;
  uint32_t lastFrameCompletedMs = 0;
};
