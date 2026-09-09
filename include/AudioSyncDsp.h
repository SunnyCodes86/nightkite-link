#pragma once

#include <stddef.h>
#include <stdint.h>

struct AudioSyncDspConfig {
  uint32_t sampleRate = 8000;
  uint8_t sensitivity = 100;
  uint8_t noiseGate = 20;
  uint8_t smoothing = 50;
  bool fullBands = false;
  bool beatDetect = false;
};

struct AudioSyncDspOutput {
  bool valid = false;
  bool beat = false;
  bool beatLocked = false;
  uint8_t energy = 0;
  uint8_t bass = 0;
  uint8_t mid = 0;
  uint8_t treble = 0;
  uint8_t confidence = 0;
  uint16_t bpm = 0;
  uint16_t beatMs = 0;
  uint32_t phaseMs = 0;
  float rms = 0.0f;
  float peak = 0.0f;
  float noiseFloor = 0.0f;
};

class AudioSyncDsp {
public:
  static constexpr size_t MAX_FRAME_SAMPLES = 256;

  void reset(uint32_t nowMs);
  void processFrame(const int16_t* samples, size_t count, uint32_t nowMs, const AudioSyncDspConfig& config);
  AudioSyncDspOutput output(uint32_t nowMs, const AudioSyncDspConfig& config) const;

private:
  static float clampFloat(float value, float minValue, float maxValue);
  static uint8_t toByte(float value);
  static float smoothValue(float current, float target, uint8_t smoothing);
  void updateBeat(float onset, float energyTarget, uint32_t nowMs, const AudioSyncDspConfig& config);
  void acceptBeat(uint32_t nowMs);

  bool hasFrames = false;
  bool signalValid = false;
  uint32_t phaseEpochMs = 0;
  uint32_t lastBeatMs = 0;
  uint32_t beatPulseUntilMs = 0;
  float rmsValue = 0.0f;
  float peakValue = 0.0f;
  float noiseFloorValue = 1.0f;
  float levelReference = 1.0f;
  float energyValue = 0.0f;
  float bassValue = 0.0f;
  float midValue = 0.0f;
  float trebleValue = 0.0f;
  float signalQuality = 0.0f;
  float beatStability = 0.0f;
  float detectedBeatMs = 0.0f;
  float previousOnset = 0.0f;
  float onsetHistory[32] = {};
  size_t onsetHistoryCount = 0;
  size_t onsetHistoryIndex = 0;
};
