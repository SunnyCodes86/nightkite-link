#include "AudioSyncDsp.h"

#include <algorithm>
#include <math.h>

namespace {

constexpr float PI = 3.14159265358979323846f;
constexpr float BASS_FREQUENCIES[] = {75.0f, 110.0f, 160.0f, 230.0f};
constexpr float MID_FREQUENCIES[] = {350.0f, 600.0f, 1000.0f, 1600.0f};
// At 8 kHz the usable treble range ends below the 4 kHz Nyquist limit.
constexpr float TREBLE_FREQUENCIES[] = {2200.0f, 2800.0f, 3400.0f};

float averageMagnitudes(const float* samples, size_t count, uint32_t sampleRate, const float* frequencies,
                        size_t frequencyCount)
{
  float sum = 0.0f;
  for (size_t i = 0; i < frequencyCount; ++i) {
    float omega = 2.0f * PI * frequencies[i] / static_cast<float>(sampleRate);
    float coefficient = 2.0f * cosf(omega);
    float q1 = 0.0f;
    float q2 = 0.0f;
    for (size_t sample = 0; sample < count; ++sample) {
      float q0 = samples[sample] + coefficient * q1 - q2;
      q2 = q1;
      q1 = q0;
    }
    float power = q1 * q1 + q2 * q2 - coefficient * q1 * q2;
    sum += sqrtf(power > 0.0f ? power : 0.0f) / static_cast<float>(count);
  }
  return frequencyCount > 0 ? sum / static_cast<float>(frequencyCount) : 0.0f;
}

}  // namespace

void AudioSyncDsp::reset(uint32_t nowMs)
{
  hasFrames = false;
  signalValid = false;
  phaseEpochMs = nowMs;
  lastBeatMs = 0;
  beatPulseUntilMs = 0;
  rmsValue = 0.0f;
  peakValue = 0.0f;
  noiseFloorValue = 1.0f;
  levelReference = 1.0f;
  energyValue = 0.0f;
  bassValue = 0.0f;
  midValue = 0.0f;
  trebleValue = 0.0f;
  signalQuality = 0.0f;
  beatStability = 0.0f;
  detectedBeatMs = 0.0f;
  previousOnset = 0.0f;
  onsetHistoryCount = 0;
  onsetHistoryIndex = 0;
  std::fill(onsetHistory, onsetHistory + (sizeof(onsetHistory) / sizeof(onsetHistory[0])), 0.0f);
}

void AudioSyncDsp::processFrame(const int16_t* samples, size_t count, uint32_t nowMs,
                                const AudioSyncDspConfig& config)
{
  if (samples == nullptr || count == 0 || count > MAX_FRAME_SAMPLES || config.sampleRate == 0) {
    return;
  }

  float mean = 0.0f;
  for (size_t i = 0; i < count; ++i) {
    mean += static_cast<float>(samples[i]);
  }
  mean /= static_cast<float>(count);

  float centered[MAX_FRAME_SAMPLES];
  float sumSquares = 0.0f;
  float peak = 0.0f;
  for (size_t i = 0; i < count; ++i) {
    float value = static_cast<float>(samples[i]) - mean;
    float absolute = fabsf(value);
    if (absolute > peak) {
      peak = absolute;
    }
    sumSquares += value * value;
    float window = count > 1 ? 0.5f - 0.5f * cosf(2.0f * PI * static_cast<float>(i) /
                                                  static_cast<float>(count - 1))
                             : 1.0f;
    centered[i] = value * window;
  }

  float rms = sqrtf(sumSquares / static_cast<float>(count));
  rmsValue = rms;
  peakValue = peak;
  if (!hasFrames) {
    noiseFloorValue = rms > 1.0f ? rms * 0.75f : 1.0f;
    levelReference = noiseFloorValue * 2.0f;
    hasFrames = true;
  } else {
    float noiseAlpha = rms < noiseFloorValue ? 0.08f : (rms < noiseFloorValue * 1.35f ? 0.01f : 0.0008f);
    noiseFloorValue += (rms - noiseFloorValue) * noiseAlpha;
    noiseFloorValue = noiseFloorValue < 1.0f ? 1.0f : noiseFloorValue;
  }

  float gateMultiplier = 1.05f + (static_cast<float>(config.noiseGate) / 100.0f) * 1.45f;
  float gateThreshold = noiseFloorValue * gateMultiplier;
  float signal = rms > gateThreshold ? rms - gateThreshold : 0.0f;
  signalValid = signal > 0.0f;
  float desiredReference = signal > noiseFloorValue * 1.5f ? signal : noiseFloorValue * 1.5f;
  float referenceAlpha = desiredReference > levelReference ? 0.15f : 0.003f;
  levelReference += (desiredReference - levelReference) * referenceAlpha;

  float sensitivityGain = static_cast<float>(config.sensitivity) / 100.0f;
  float normalized = signal * sensitivityGain / (levelReference + noiseFloorValue * 0.5f + 1.0f);
  float energyTarget = 255.0f * clampFloat(normalized, 0.0f, 1.0f);
  energyValue = smoothValue(energyValue, energyTarget, config.smoothing);

  float snr = rms / (noiseFloorValue + 1.0f);
  float qualityTarget = clampFloat((snr - 1.0f) / 2.5f, 0.0f, 1.0f);
  float qualityAlpha = qualityTarget > signalQuality ? 0.25f : 0.04f;
  signalQuality += (qualityTarget - signalQuality) * qualityAlpha;

  if (config.fullBands && energyTarget > 0.0f) {
    float bassMagnitude = averageMagnitudes(centered, count, config.sampleRate, BASS_FREQUENCIES,
                                            sizeof(BASS_FREQUENCIES) / sizeof(BASS_FREQUENCIES[0]));
    float midMagnitude = averageMagnitudes(centered, count, config.sampleRate, MID_FREQUENCIES,
                                           sizeof(MID_FREQUENCIES) / sizeof(MID_FREQUENCIES[0]));
    float trebleMagnitude = averageMagnitudes(centered, count, config.sampleRate, TREBLE_FREQUENCIES,
                                               sizeof(TREBLE_FREQUENCIES) / sizeof(TREBLE_FREQUENCIES[0]));
    float magnitudeSum = bassMagnitude + midMagnitude + trebleMagnitude + 0.001f;
    float bassTarget = energyTarget * clampFloat((bassMagnitude / magnitudeSum) * 2.4f, 0.0f, 1.2f);
    float midTarget = energyTarget * clampFloat((midMagnitude / magnitudeSum) * 2.4f, 0.0f, 1.2f);
    float trebleTarget = energyTarget * clampFloat((trebleMagnitude / magnitudeSum) * 2.4f, 0.0f, 1.2f);
    bassValue = smoothValue(bassValue, bassTarget, config.smoothing);
    midValue = smoothValue(midValue, midTarget, config.smoothing);
    trebleValue = smoothValue(trebleValue, trebleTarget, config.smoothing);
    updateBeat(bassTarget * 0.7f + energyTarget * 0.3f, energyTarget, nowMs, config);
  } else {
    bassValue = smoothValue(bassValue, 0.0f, config.smoothing);
    midValue = smoothValue(midValue, 0.0f, config.smoothing);
    trebleValue = smoothValue(trebleValue, 0.0f, config.smoothing);
    previousOnset = 0.0f;
  }

  if (lastBeatMs > 0 && detectedBeatMs > 0.0f &&
      nowMs - lastBeatMs > static_cast<uint32_t>(detectedBeatMs * 2.5f)) {
    beatStability *= 0.97f;
  }
}

AudioSyncDspOutput AudioSyncDsp::output(uint32_t nowMs, const AudioSyncDspConfig& config) const
{
  AudioSyncDspOutput result;
  result.valid = hasFrames && signalValid;
  result.rms = rmsValue;
  result.peak = peakValue;
  result.noiseFloor = noiseFloorValue;
  if (!result.valid) {
    return result;
  }
  result.energy = toByte(energyValue);
  result.bass = toByte(bassValue);
  result.mid = toByte(midValue);
  result.treble = toByte(trebleValue);

  bool detected = config.beatDetect && detectedBeatMs > 0.0f && lastBeatMs > 0 && beatStability > 0.25f &&
                  nowMs - lastBeatMs <= static_cast<uint32_t>(detectedBeatMs * 2.5f);
  result.beatLocked = detected;
  if (detected) {
    result.beatMs = static_cast<uint16_t>(clampFloat(detectedBeatMs, 333.0f, 1000.0f));
    result.bpm = static_cast<uint16_t>(60000UL / result.beatMs);
    result.phaseMs = (nowMs - phaseEpochMs) % result.beatMs;
  }
  result.beat = config.beatDetect && static_cast<int32_t>(beatPulseUntilMs - nowMs) > 0;

  float confidence = signalQuality;
  if (config.beatDetect) {
    confidence *= detected ? (0.35f + beatStability * 0.65f) : 0.35f;
  } else {
    confidence *= 0.65f;
  }
  result.confidence = toByte(confidence * 255.0f);
  return result;
}

float AudioSyncDsp::clampFloat(float value, float minValue, float maxValue)
{
  return value < minValue ? minValue : (value > maxValue ? maxValue : value);
}

uint8_t AudioSyncDsp::toByte(float value)
{
  return static_cast<uint8_t>(clampFloat(value, 0.0f, 255.0f) + 0.5f);
}

float AudioSyncDsp::smoothValue(float current, float target, uint8_t smoothing)
{
  float smooth = clampFloat(static_cast<float>(smoothing) / 100.0f, 0.0f, 1.0f);
  float alpha = target > current ? 0.70f - smooth * 0.35f : 0.18f - smooth * 0.14f;
  return current + (target - current) * alpha;
}

void AudioSyncDsp::updateBeat(float onset, float energyTarget, uint32_t nowMs, const AudioSyncDspConfig& config)
{
  float average = 0.0f;
  for (size_t i = 0; i < onsetHistoryCount; ++i) {
    average += onsetHistory[i];
  }
  if (onsetHistoryCount > 0) {
    average /= static_cast<float>(onsetHistoryCount);
  }

  bool refractoryDone = lastBeatMs == 0 || nowMs - lastBeatMs >= 280;
  bool rising = onset > previousOnset * 1.05f;
  bool candidate = config.beatDetect && onsetHistoryCount >= 8 && energyTarget >= 28.0f && refractoryDone && rising &&
                   onset > average * 1.45f + 8.0f;
  if (candidate) {
    acceptBeat(nowMs);
  }

  onsetHistory[onsetHistoryIndex] = onset;
  onsetHistoryIndex = (onsetHistoryIndex + 1) % (sizeof(onsetHistory) / sizeof(onsetHistory[0]));
  if (onsetHistoryCount < sizeof(onsetHistory) / sizeof(onsetHistory[0])) {
    ++onsetHistoryCount;
  }
  previousOnset = onset;
}

void AudioSyncDsp::acceptBeat(uint32_t nowMs)
{
  beatPulseUntilMs = nowMs + 120;
  if (lastBeatMs == 0) {
    lastBeatMs = nowMs;
    phaseEpochMs = nowMs;
    return;
  }

  uint32_t interval = nowMs - lastBeatMs;
  lastBeatMs = nowMs;
  if (interval < 333 || interval > 1000) {
    beatStability *= 0.75f;
    return;
  }

  if (beatStability == 0.0f) {
    detectedBeatMs = static_cast<float>(interval);
    beatStability = 0.35f;
    phaseEpochMs = nowMs;
    return;
  }

  float error = fabsf(static_cast<float>(interval) - detectedBeatMs) / detectedBeatMs;
  float intervalStability = clampFloat(1.0f - error * 3.0f, 0.0f, 1.0f);
  beatStability += (intervalStability - beatStability) * 0.25f;
  if (error < 0.35f) {
    detectedBeatMs += (static_cast<float>(interval) - detectedBeatMs) * 0.20f;
  }

  uint32_t period = static_cast<uint32_t>(detectedBeatMs);
  if (period > 0) {
    int32_t phase = static_cast<int32_t>((nowMs - phaseEpochMs) % period);
    int32_t phaseError = phase <= static_cast<int32_t>(period / 2) ? phase : phase - static_cast<int32_t>(period);
    float correction = beatStability > 0.6f ? 0.55f : 0.25f;
    phaseEpochMs += static_cast<int32_t>(static_cast<float>(phaseError) * correction);
  }
}
