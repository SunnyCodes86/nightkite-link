#include "AudioSyncDsp.h"

#include <algorithm>
#include <math.h>

#if defined(ESP_PLATFORM)
#include <esp_dsp.h>
#include <esp_timer.h>
#endif

namespace {

constexpr float PI = 3.14159265358979323846f;
constexpr uint32_t AUDIO_ACTIVE_HOLD_MS = 400;
constexpr uint32_t TRACKER_SILENCE_RESET_MS = 1200;
constexpr uint32_t BEAT_REFRACTORY_MS = 180;
constexpr float MIN_BPM = 60.0f;
constexpr float MAX_BPM = 180.0f;

// 16 kHz / 512 bins: 62.5 Hz through 7.531 kHz, with logarithmically widening bands.
constexpr uint16_t BAND_EDGES[AudioSyncDsp::BAND_COUNT + 1] = {
    2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 64, 88, 112, 144, 176, 208, 241};

// Intentionally conservative until the installed Cardputer ADV microphone is measured.
constexpr float BAND_COMPENSATION[AudioSyncDsp::BAND_COUNT] = {
    1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f,
    1.00f, 1.00f, 1.00f, 1.02f, 1.04f, 1.06f, 1.08f, 1.08f};

#if defined(ESP_PLATFORM)
alignas(16) float fftTable[AudioSyncDsp::FFT_SAMPLES];
bool espFftInitialized = false;
#endif

float bandPower(const float* spectrum, size_t begin, size_t end)
{
  float sum = 0.0f;
  for (size_t bin = begin; bin < end; ++bin) sum += spectrum[bin] * spectrum[bin];
  return sqrtf(sum * 0.5f);
}

#if !defined(ESP_PLATFORM)
void hostFft(float* data, size_t count)
{
  for (size_t i = 1, j = 0; i < count; ++i) {
    size_t bit = count >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) {
      std::swap(data[i * 2], data[j * 2]);
      std::swap(data[i * 2 + 1], data[j * 2 + 1]);
    }
  }
  for (size_t length = 2; length <= count; length <<= 1) {
    float angle = -2.0f * PI / static_cast<float>(length);
    float stepReal = cosf(angle);
    float stepImag = sinf(angle);
    for (size_t start = 0; start < count; start += length) {
      float twiddleReal = 1.0f;
      float twiddleImag = 0.0f;
      for (size_t offset = 0; offset < length / 2; ++offset) {
        size_t even = start + offset;
        size_t odd = even + length / 2;
        float oddReal = data[odd * 2] * twiddleReal - data[odd * 2 + 1] * twiddleImag;
        float oddImag = data[odd * 2] * twiddleImag + data[odd * 2 + 1] * twiddleReal;
        float evenReal = data[even * 2];
        float evenImag = data[even * 2 + 1];
        data[even * 2] = evenReal + oddReal;
        data[even * 2 + 1] = evenImag + oddImag;
        data[odd * 2] = evenReal - oddReal;
        data[odd * 2 + 1] = evenImag - oddImag;
        float nextReal = twiddleReal * stepReal - twiddleImag * stepImag;
        twiddleImag = twiddleReal * stepImag + twiddleImag * stepReal;
        twiddleReal = nextReal;
      }
    }
  }
}
#endif

}  // namespace

void AudioSyncDsp::reset(uint32_t nowMs)
{
  initializeBuffers();
  hasFrames = false;
  rawGateValue = false;
  audioActive = false;
  onsetCandidateValue = false;
  acceptedBeatValue = false;
  rollingSamples = 0;
  lastFrameMs = nowMs;
  lastRawGateMs = 0;
  silenceStartedMs = nowMs;
  rmsValue = 0.0f;
  peakValue = 0.0f;
  noiseFloorValue = 1.0f;
  agcReference = 1.0f;
  energyRawValue = 0.0f;
  energyValue = 0.0f;
  bassValue = 0.0f;
  midValue = 0.0f;
  trebleValue = 0.0f;
  spectralFluxValue = 0.0f;
  onsetThresholdValue = 0.0f;
  previousFlux = 0.0f;
  previousPreviousFlux = 0.0f;
  previousFluxThreshold = 0.0f;
  previousFluxMs = nowMs;
  previousFluxGate = false;
  fluxHistoryCount = 0;
  fluxHistoryIndex = 0;
  clippingSamplesValue = 0;
  processedSamplesValue = 0;
  runtimeUsValue = 0;
  runtimeHistoryCount = 0;
  runtimeHistoryIndex = 0;
  clearBeatTracker(nowMs);
  std::fill(rollingPcm, rollingPcm + FFT_SAMPLES, 0.0f);
  std::fill(spectrum, spectrum + SPECTRUM_BINS, 0.0f);
  std::fill(spectralEnvelope, spectralEnvelope + SPECTRUM_BINS, 0.0f);
  std::fill(previousWhitened, previousWhitened + SPECTRUM_BINS, 0.0f);
  std::fill(fluxHistory, fluxHistory + FLUX_HISTORY_SIZE, 0.0f);
  std::fill(bandRaw, bandRaw + BAND_COUNT, 0.0f);
  std::fill(bandValue, bandValue + BAND_COUNT, 0.0f);
  std::fill(runtimeHistory, runtimeHistory + RUNTIME_HISTORY_SIZE, 0U);
}

void AudioSyncDsp::processFrame(const int16_t* samples, size_t count, uint32_t nowMs,
                                const AudioSyncDspConfig& config)
{
  if (samples == nullptr || count != HOP_SAMPLES || config.sampleRate != 16000) return;

#if defined(ESP_PLATFORM)
  int64_t runtimeStartedUs = esp_timer_get_time();
#endif
  onsetCandidateValue = false;
  acceptedBeatValue = false;
  float dtMs = hasFrames ? clampFloat(static_cast<float>(nowMs - lastFrameMs), 1.0f, 100.0f) : 16.0f;
  lastFrameMs = nowMs;

  float mean = 0.0f;
  for (size_t i = 0; i < count; ++i) mean += static_cast<float>(samples[i]);
  mean /= static_cast<float>(count);

  float sumSquares = 0.0f;
  float peak = 0.0f;
  for (size_t i = 0; i < count; ++i) {
    float value = static_cast<float>(samples[i]) - mean;
    float absolute = fabsf(value);
    peak = std::max(peak, absolute);
    sumSquares += value * value;
    rollingPcm[i] = rollingPcm[i + HOP_SAMPLES];
    rollingPcm[i + HOP_SAMPLES] = static_cast<float>(samples[i]);
    if (abs(static_cast<int>(samples[i])) >= 32760) ++clippingSamplesValue;
  }
  processedSamplesValue += count;
  rollingSamples = std::min<uint32_t>(rollingSamples + count, FFT_SAMPLES);
  rmsValue = sqrtf(sumSquares / static_cast<float>(count));
  peakValue = peak;
  updateGate(rmsValue, nowMs, dtMs, config);
  hasFrames = true;

  if (rollingSamples == FFT_SAMPLES && calculateSpectrum()) {
    updateVisualizer(dtMs, config);
    updateOnset(nowMs, dtMs, config);
  } else {
    energyRawValue = rawGateValue ? rmsValue : 0.0f;
  }
  updateTrackerAging(nowMs, dtMs);

#if defined(ESP_PLATFORM)
  recordRuntime(static_cast<uint32_t>(esp_timer_get_time() - runtimeStartedUs));
#endif
}

AudioSyncDspOutput AudioSyncDsp::output(uint32_t nowMs, const AudioSyncDspConfig& config) const
{
  AudioSyncDspOutput result;
  result.valid = hasFrames && audioActive;
  result.rawGate = rawGateValue;
  result.rms = rmsValue;
  result.peak = peakValue;
  result.noiseFloor = noiseFloorValue;
  result.energyRaw = energyRawValue;
  result.spectralFlux = spectralFluxValue;
  result.onsetThreshold = onsetThresholdValue;
  result.onsetCandidate = onsetCandidateValue;
  result.acceptedBeat = acceptedBeatValue;
  result.bestTempoBpm = bestTempoBpm;
  result.phaseErrorMs = lastPhaseErrorMs;
  result.dominantBin = dominantBinValue;
  result.clippingSamples = clippingSamplesValue;
  result.processedSamples = processedSamplesValue;
  result.dspRuntimeUs = runtimeUsValue;
  result.dspRuntimeP50Us = runtimePercentile(50, 100);
  result.dspRuntimeP95Us = runtimePercentile(95, 100);
  result.dspRuntimeP99Us = runtimePercentile(99, 100);
  for (size_t band = 0; band < BAND_COUNT; ++band) result.bands[band] = bandValue[band];
  if (!result.valid) return result;

  result.energy = toByte(energyValue);
  result.bass = config.fullBands ? toByte(bassValue) : 0;
  result.mid = config.fullBands ? toByte(midValue) : 0;
  result.treble = config.fullBands ? toByte(trebleValue) : 0;
  bool locked = config.beatDetect && beatLockedValue && detectedBeatMs > 0.0f;
  result.beatLocked = locked;
  result.beat = config.beatDetect && static_cast<int32_t>(beatPulseUntilMs - nowMs) > 0;
  if (locked) {
    result.beatMs = static_cast<uint16_t>(clampFloat(detectedBeatMs, 333.0f, 1000.0f) + 0.5f);
    result.bpm = static_cast<uint16_t>(60000.0f / static_cast<float>(result.beatMs) + 0.5f);
    result.phaseMs = (nowMs - phaseEpochMs) % result.beatMs;
  }
  float confidence = config.beatDetect
                         ? trackerConfidence
                         : clampFloat(rmsValue / (noiseFloorValue * 3.0f + 1.0f), 0.0f, 1.0f);
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

float AudioSyncDsp::timeAlpha(float dtMs, float timeConstantMs)
{
  return 1.0f - expf(-dtMs / std::max(timeConstantMs, 1.0f));
}

float AudioSyncDsp::smoothValue(float current, float target, float dtMs, uint8_t smoothing)
{
  float amount = clampFloat(static_cast<float>(smoothing), 0.0f, 100.0f);
  float timeMs = target > current ? 20.0f + amount : 100.0f + amount * 6.0f;
  return current + (target - current) * timeAlpha(dtMs, timeMs);
}

void AudioSyncDsp::initializeBuffers()
{
  if (initialized) return;
  for (size_t i = 0; i < FFT_SAMPLES; ++i) {
    window[i] = 0.5f - 0.5f * cosf(2.0f * PI * static_cast<float>(i) / static_cast<float>(FFT_SAMPLES - 1));
  }
#if defined(ESP_PLATFORM)
  if (!espFftInitialized) espFftInitialized = dsps_fft2r_init_fc32(fftTable, FFT_SAMPLES) == ESP_OK;
#endif
  initialized = true;
}

bool AudioSyncDsp::calculateSpectrum()
{
#if defined(ESP_PLATFORM)
  if (!espFftInitialized) return false;
#endif
  float mean = 0.0f;
  for (size_t i = 0; i < FFT_SAMPLES; ++i) mean += rollingPcm[i];
  mean /= static_cast<float>(FFT_SAMPLES);
  for (size_t i = 0; i < FFT_SAMPLES; ++i) {
    fftData[i * 2] = (rollingPcm[i] - mean) * window[i];
    fftData[i * 2 + 1] = 0.0f;
  }
#if defined(ESP_PLATFORM)
  if (dsps_fft2r_fc32(fftData, FFT_SAMPLES) != ESP_OK || dsps_bit_rev_fc32(fftData, FFT_SAMPLES) != ESP_OK) {
    return false;
  }
#else
  hostFft(fftData, FFT_SAMPLES);
#endif
  constexpr float amplitudeScale = 2.0f / static_cast<float>(FFT_SAMPLES);
  for (size_t bin = 0; bin < SPECTRUM_BINS; ++bin) {
    float real = fftData[bin * 2];
    float imaginary = fftData[bin * 2 + 1];
    spectrum[bin] = sqrtf(real * real + imaginary * imaginary) * amplitudeScale;
  }
  spectrum[0] = 0.0f;
  dominantBinValue = static_cast<uint16_t>(std::max_element(spectrum + 1, spectrum + SPECTRUM_BINS) - spectrum);
  return true;
}

void AudioSyncDsp::updateGate(float rms, uint32_t nowMs, float dtMs, const AudioSyncDspConfig& config)
{
  if (!hasFrames) noiseFloorValue = std::max(1.0f, rms);
  float multiplier = 1.05f + static_cast<float>(config.noiseGate) * 0.0145f;
  float openThreshold = noiseFloorValue * multiplier;
  float closeThreshold = noiseFloorValue * (1.0f + (multiplier - 1.0f) * 0.55f);
  rawGateValue = rms > (rawGateValue ? closeThreshold : openThreshold);

  if (!rawGateValue) {
    float timeMs = rms < noiseFloorValue ? 250.0f : 2500.0f;
    noiseFloorValue += (rms - noiseFloorValue) * timeAlpha(dtMs, timeMs);
    noiseFloorValue = std::max(1.0f, noiseFloorValue);
  }
  if (rawGateValue) {
    lastRawGateMs = nowMs;
    silenceStartedMs = 0;
    audioActive = true;
  } else {
    if (silenceStartedMs == 0) silenceStartedMs = nowMs;
    if (lastRawGateMs == 0 || nowMs - lastRawGateMs > AUDIO_ACTIVE_HOLD_MS) audioActive = false;
  }
}

void AudioSyncDsp::updateVisualizer(float dtMs, const AudioSyncDspConfig& config)
{
  for (size_t band = 0; band < BAND_COUNT; ++band) {
    bandRaw[band] = bandPower(spectrum, BAND_EDGES[band], BAND_EDGES[band + 1]) * BAND_COMPENSATION[band];
  }
  float signal = rawGateValue ? std::max(0.0f, rmsValue - noiseFloorValue) : 0.0f;
  energyRawValue = signal;
  float desiredReference = std::max(signal, noiseFloorValue * 1.5f);
  float agcTimeMs = desiredReference > agcReference ? 220.0f : 4500.0f;
  if (rawGateValue || desiredReference < agcReference) {
    agcReference += (desiredReference - agcReference) * timeAlpha(dtMs, agcTimeMs);
  }
  float commonGain = 200.0f * (static_cast<float>(config.sensitivity) / 100.0f) /
                     std::max(agcReference, noiseFloorValue * 1.5f);
  float energyTarget = rawGateValue ? clampFloat(signal * commonGain, 0.0f, 255.0f) : 0.0f;
  energyValue = smoothValue(energyValue, energyTarget, dtMs, config.smoothing);

  for (size_t band = 0; band < BAND_COUNT; ++band) {
    float target = rawGateValue ? clampFloat(bandRaw[band] * commonGain, 0.0f, 255.0f) : 0.0f;
    bandValue[band] = smoothValue(bandValue[band], target, dtMs, config.smoothing);
  }
  float bassTarget = rawGateValue ? clampFloat(bandPower(spectrum, 2, 8) * commonGain, 0.0f, 255.0f) : 0.0f;
  float midTarget = rawGateValue ? clampFloat(bandPower(spectrum, 8, 64) * commonGain, 0.0f, 255.0f) : 0.0f;
  float trebleTarget = rawGateValue ? clampFloat(bandPower(spectrum, 64, 241) * commonGain, 0.0f, 255.0f) : 0.0f;
  bassValue = smoothValue(bassValue, bassTarget, dtMs, config.smoothing);
  midValue = smoothValue(midValue, midTarget, dtMs, config.smoothing);
  trebleValue = smoothValue(trebleValue, trebleTarget, dtMs, config.smoothing);
}

void AudioSyncDsp::updateOnset(uint32_t nowMs, float dtMs, const AudioSyncDspConfig& config)
{
  float flux = 0.0f;
  float weightSum = 0.0f;
  float envelopeDecay = expf(-dtMs / 1200.0f);
  for (size_t bin = 2; bin < 241; ++bin) {
    float logMagnitude = log1pf(spectrum[bin]);
    spectralEnvelope[bin] = std::max(logMagnitude, spectralEnvelope[bin] * envelopeDecay);
    float whitened = logMagnitude / (spectralEnvelope[bin] + 0.05f);
    float delta = std::max(0.0f, whitened - previousWhitened[bin]);
    float weight = bin < 8 ? 1.5f : (bin < 64 ? 1.0f : 0.35f);
    flux += delta * weight;
    weightSum += weight;
    previousWhitened[bin] = whitened;
  }
  flux = weightSum > 0.0f ? flux / weightSum : 0.0f;
  spectralFluxValue = flux;

  float sorted[FLUX_HISTORY_SIZE];
  for (size_t i = 0; i < fluxHistoryCount; ++i) sorted[i] = fluxHistory[i];
  std::sort(sorted, sorted + fluxHistoryCount);
  float median = fluxHistoryCount ? sorted[fluxHistoryCount / 2] : 0.0f;
  for (size_t i = 0; i < fluxHistoryCount; ++i) sorted[i] = fabsf(fluxHistory[i] - median);
  std::sort(sorted, sorted + fluxHistoryCount);
  float mad = fluxHistoryCount ? sorted[fluxHistoryCount / 2] : 0.0f;
  onsetThresholdValue = median + mad * 3.0f + 0.004f;

  bool localMaximum = previousFlux > previousPreviousFlux && previousFlux >= flux;
  bool refractoryDone = lastAcceptedOnsetMs == 0 || previousFluxMs - lastAcceptedOnsetMs >= BEAT_REFRACTORY_MS;
  onsetCandidateValue = config.beatDetect && fluxHistoryCount >= 16 && previousFluxGate && localMaximum &&
                        previousFlux > previousFluxThreshold;
  if (onsetCandidateValue && refractoryDone) acceptOnset(previousFluxMs);

  fluxHistory[fluxHistoryIndex] = flux;
  fluxHistoryIndex = (fluxHistoryIndex + 1) % FLUX_HISTORY_SIZE;
  if (fluxHistoryCount < FLUX_HISTORY_SIZE) ++fluxHistoryCount;
  previousPreviousFlux = previousFlux;
  previousFlux = flux;
  previousFluxThreshold = onsetThresholdValue;
  previousFluxMs = nowMs;
  previousFluxGate = rawGateValue;
}

void AudioSyncDsp::acceptOnset(uint32_t onsetMs)
{
  acceptedBeatValue = true;
  beatPulseUntilMs = onsetMs + 120;
  updateTempo(onsetMs);
  lastAcceptedOnsetMs = onsetMs;
  onsetTimes[onsetIndex] = onsetMs;
  onsetIndex = (onsetIndex + 1) % ONSET_HISTORY_SIZE;
  if (onsetCount < ONSET_HISTORY_SIZE) ++onsetCount;
}

void AudioSyncDsp::updateTempo(uint32_t onsetMs)
{
  for (size_t bin = 0; bin < TEMPO_BIN_COUNT; ++bin) tempoVotes[bin] *= 0.93f;
  size_t comparisons = std::min<size_t>(onsetCount, 12);
  for (size_t age = 0; age < comparisons; ++age) {
    size_t index = (onsetIndex + ONSET_HISTORY_SIZE - 1 - age) % ONSET_HISTORY_SIZE;
    uint32_t interval = onsetMs - onsetTimes[index];
    if (interval < BEAT_REFRACTORY_MS || interval > 4000) continue;
    for (uint8_t beats = 1; beats <= 6; ++beats) {
      float bpm = 60000.0f * static_cast<float>(beats) / static_cast<float>(interval);
      if (bpm < MIN_BPM || bpm > MAX_BPM) continue;
      float vote = (1.0f / static_cast<float>(beats)) * (1.0f - static_cast<float>(age) * 0.035f);
      int center = static_cast<int>(bpm - MIN_BPM + 0.5f);
      for (int offset = -2; offset <= 2; ++offset) {
        int bin = center + offset;
        if (bin >= 0 && bin < static_cast<int>(TEMPO_BIN_COUNT)) {
          tempoVotes[bin] += vote * (1.0f - fabsf(static_cast<float>(offset)) * 0.25f);
        }
      }
    }
  }

  size_t bestBin = 0;
  float bestScore = 0.0f;
  auto scoreForBin = [this](size_t bin) {
    float bpm = MIN_BPM + static_cast<float>(bin);
    float prior = detectedBeatMs > 0.0f
                      ? 1.0f + (beatLockedValue ? 1.0f : 0.25f) *
                                   expf(-fabsf(bpm - 60000.0f / detectedBeatMs) / 6.0f)
                      : 1.0f;
    return tempoVotes[bin] * prior;
  };
  for (size_t bin = 0; bin < TEMPO_BIN_COUNT; ++bin) {
    float score = scoreForBin(bin);
    if (score > bestScore) {
      bestScore = score;
      bestBin = bin;
    }
  }
  if (bestScore <= 0.0f) return;

  float secondScore = 0.0f;
  for (size_t bin = 0; bin < TEMPO_BIN_COUNT; ++bin) {
    if (abs(static_cast<int>(bin) - static_cast<int>(bestBin)) <= 3) continue;
    secondScore = std::max(secondScore, scoreForBin(bin));
  }

  bestTempoBpm = MIN_BPM + static_cast<float>(bestBin);
  float candidatePeriod = 60000.0f / bestTempoBpm;
  float dominance = clampFloat((bestScore - secondScore) / bestScore, 0.0f, 1.0f);
  float support = clampFloat(static_cast<float>(onsetCount + 1) / 7.0f, 0.0f, 1.0f);
  float phaseConsistency = 0.5f;

  if (detectedBeatMs <= 0.0f) {
    detectedBeatMs = candidatePeriod;
    phaseEpochMs = onsetMs;
    lastPhaseErrorMs = 0.0f;
  } else {
    float maxPeriodStep = detectedBeatMs * (beatLockedValue ? 0.05f : 0.10f);
    float periodStep = clampFloat(candidatePeriod - detectedBeatMs, -maxPeriodStep, maxPeriodStep);
    detectedBeatMs += periodStep * (beatLockedValue ? 0.70f : 0.80f);
    int32_t period = static_cast<int32_t>(detectedBeatMs + 0.5f);
    int32_t phase = static_cast<int32_t>((onsetMs - phaseEpochMs) % static_cast<uint32_t>(period));
    int32_t error = phase <= period / 2 ? phase : phase - period;
    lastPhaseErrorMs = static_cast<float>(error);
    float phaseLimit = detectedBeatMs * 0.22f;
    if (fabsf(lastPhaseErrorMs) <= phaseLimit) {
      phaseConsistency = 1.0f - fabsf(lastPhaseErrorMs) / phaseLimit;
      float maxCorrection = detectedBeatMs * 0.08f;
      float correction =
          clampFloat(lastPhaseErrorMs * (beatLockedValue ? 0.22f : 0.35f), -maxCorrection, maxCorrection);
      phaseEpochMs += static_cast<int32_t>(correction);
    } else {
      phaseConsistency = 0.0f;
    }
  }

  float evidence = dominance * 0.45f + support * 0.30f + phaseConsistency * 0.25f;
  trackerConfidence += (evidence - trackerConfidence) * 0.30f;
  lockEvidence += evidence >= 0.42f ? 0.20f : -0.03f;
  lockEvidence = clampFloat(lockEvidence, 0.0f, 1.0f);
  if (!beatLockedValue && lockEvidence >= 0.60f) beatLockedValue = true;
  if (beatLockedValue && lockEvidence < 0.25f) beatLockedValue = false;
}

void AudioSyncDsp::updateTrackerAging(uint32_t nowMs, float dtMs)
{
  if (silenceStartedMs != 0 && nowMs - silenceStartedMs >= TRACKER_SILENCE_RESET_MS) {
    clearBeatTracker(nowMs);
    return;
  }
  if (lastAcceptedOnsetMs != 0 && detectedBeatMs > 0.0f &&
      nowMs - lastAcceptedOnsetMs > static_cast<uint32_t>(detectedBeatMs * 2.5f)) {
    trackerConfidence *= expf(-dtMs / 2500.0f);
    lockEvidence -= timeAlpha(dtMs, 3000.0f) * 0.35f;
    lockEvidence = std::max(0.0f, lockEvidence);
    if (lockEvidence < 0.25f) beatLockedValue = false;
  }
}

void AudioSyncDsp::clearBeatTracker(uint32_t nowMs)
{
  beatLockedValue = false;
  phaseEpochMs = nowMs;
  lastAcceptedOnsetMs = 0;
  beatPulseUntilMs = 0;
  bestTempoBpm = 0.0f;
  detectedBeatMs = 0.0f;
  trackerConfidence = 0.0f;
  lockEvidence = 0.0f;
  lastPhaseErrorMs = 0.0f;
  onsetCount = 0;
  onsetIndex = 0;
  std::fill(onsetTimes, onsetTimes + ONSET_HISTORY_SIZE, 0U);
  std::fill(tempoVotes, tempoVotes + TEMPO_BIN_COUNT, 0.0f);
}

void AudioSyncDsp::recordRuntime(uint32_t runtimeUs)
{
  runtimeUsValue = runtimeUs;
  runtimeHistory[runtimeHistoryIndex] = runtimeUs;
  runtimeHistoryIndex = (runtimeHistoryIndex + 1) % RUNTIME_HISTORY_SIZE;
  if (runtimeHistoryCount < RUNTIME_HISTORY_SIZE) ++runtimeHistoryCount;
}

uint32_t AudioSyncDsp::runtimePercentile(size_t numerator, size_t denominator) const
{
  if (runtimeHistoryCount == 0) return 0;
  uint32_t sorted[RUNTIME_HISTORY_SIZE];
  for (size_t i = 0; i < runtimeHistoryCount; ++i) sorted[i] = runtimeHistory[i];
  std::sort(sorted, sorted + runtimeHistoryCount);
  size_t index = ((runtimeHistoryCount - 1) * numerator + denominator - 1) / denominator;
  return sorted[std::min(index, runtimeHistoryCount - 1)];
}
