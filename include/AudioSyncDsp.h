#pragma once

#include <stddef.h>
#include <stdint.h>

struct AudioSyncDspConfig {
  uint32_t sampleRate = 16000;
  uint8_t sensitivity = 100;
  uint8_t noiseGate = 20;
  uint8_t smoothing = 50;
  bool fullBands = false;
  bool beatDetect = false;
};

struct AudioSyncDspOutput {
  static constexpr size_t BAND_COUNT = 16;

  bool valid = false;
  bool rawGate = false;
  bool beat = false;
  bool beatLocked = false;
  bool onsetCandidate = false;
  bool acceptedBeat = false;
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
  float energyRaw = 0.0f;
  float spectralFlux = 0.0f;
  float onsetThreshold = 0.0f;
  float bestTempoBpm = 0.0f;
  float phaseErrorMs = 0.0f;
  uint16_t dominantBin = 0;
  float bands[BAND_COUNT] = {};
  uint32_t clippingSamples = 0;
  uint32_t processedSamples = 0;
  uint32_t dspRuntimeUs = 0;
  uint32_t dspRuntimeP50Us = 0;
  uint32_t dspRuntimeP95Us = 0;
  uint32_t dspRuntimeP99Us = 0;
};

class AudioSyncDsp {
public:
  static constexpr size_t HOP_SAMPLES = 256;
  static constexpr size_t FFT_SAMPLES = 512;
  static constexpr size_t SPECTRUM_BINS = FFT_SAMPLES / 2;
  static constexpr size_t BAND_COUNT = AudioSyncDspOutput::BAND_COUNT;

  void reset(uint32_t nowMs);
  void processFrame(const int16_t* samples, size_t count, uint32_t nowMs, const AudioSyncDspConfig& config);
  AudioSyncDspOutput output(uint32_t nowMs, const AudioSyncDspConfig& config) const;

private:
  static constexpr size_t FLUX_HISTORY_SIZE = 64;
  static constexpr size_t ONSET_HISTORY_SIZE = 24;
  static constexpr size_t TEMPO_BIN_COUNT = 121;
  static constexpr size_t RUNTIME_HISTORY_SIZE = 64;

  static float clampFloat(float value, float minValue, float maxValue);
  static uint8_t toByte(float value);
  static float timeAlpha(float dtMs, float timeConstantMs);
  static float smoothValue(float current, float target, float dtMs, uint8_t smoothing);
  void initializeBuffers();
  bool calculateSpectrum();
  void updateGate(float rms, uint32_t nowMs, float dtMs, const AudioSyncDspConfig& config);
  void updateVisualizer(float dtMs, const AudioSyncDspConfig& config);
  void updateOnset(uint32_t nowMs, float dtMs, const AudioSyncDspConfig& config);
  void acceptOnset(uint32_t onsetMs);
  void updateTempo(uint32_t onsetMs);
  void updateTrackerAging(uint32_t nowMs, float dtMs);
  void clearBeatTracker(uint32_t nowMs);
  void recordRuntime(uint32_t runtimeUs);
  uint32_t runtimePercentile(size_t numerator, size_t denominator) const;

  bool initialized = false;
  bool hasFrames = false;
  bool rawGateValue = false;
  bool audioActive = false;
  bool onsetCandidateValue = false;
  bool acceptedBeatValue = false;
  bool beatLockedValue = false;
  uint32_t rollingSamples = 0;
  uint32_t lastFrameMs = 0;
  uint32_t lastRawGateMs = 0;
  uint32_t silenceStartedMs = 0;
  uint32_t phaseEpochMs = 0;
  uint32_t lastAcceptedOnsetMs = 0;
  uint32_t beatPulseUntilMs = 0;
  float rmsValue = 0.0f;
  float peakValue = 0.0f;
  float noiseFloorValue = 1.0f;
  float agcReference = 1.0f;
  float energyRawValue = 0.0f;
  float energyValue = 0.0f;
  float bassValue = 0.0f;
  float midValue = 0.0f;
  float trebleValue = 0.0f;
  float spectralFluxValue = 0.0f;
  float onsetThresholdValue = 0.0f;
  float previousFlux = 0.0f;
  float previousPreviousFlux = 0.0f;
  float previousFluxThreshold = 0.0f;
  uint32_t previousFluxMs = 0;
  bool previousFluxGate = false;
  float bestTempoBpm = 0.0f;
  float detectedBeatMs = 0.0f;
  float trackerConfidence = 0.0f;
  float lockEvidence = 0.0f;
  float lastPhaseErrorMs = 0.0f;
  uint16_t dominantBinValue = 0;
  float bandRaw[BAND_COUNT] = {};
  float bandValue[BAND_COUNT] = {};
  float rollingPcm[FFT_SAMPLES] = {};
  float window[FFT_SAMPLES] = {};
  alignas(16) float fftData[FFT_SAMPLES * 2] = {};
  float spectrum[SPECTRUM_BINS] = {};
  float spectralEnvelope[SPECTRUM_BINS] = {};
  float previousWhitened[SPECTRUM_BINS] = {};
  float fluxHistory[FLUX_HISTORY_SIZE] = {};
  size_t fluxHistoryCount = 0;
  size_t fluxHistoryIndex = 0;
  uint32_t onsetTimes[ONSET_HISTORY_SIZE] = {};
  size_t onsetCount = 0;
  size_t onsetIndex = 0;
  float tempoVotes[TEMPO_BIN_COUNT] = {};
  uint32_t clippingSamplesValue = 0;
  uint32_t processedSamplesValue = 0;
  uint32_t runtimeUsValue = 0;
  uint32_t runtimeHistory[RUNTIME_HISTORY_SIZE] = {};
  size_t runtimeHistoryCount = 0;
  size_t runtimeHistoryIndex = 0;
};
