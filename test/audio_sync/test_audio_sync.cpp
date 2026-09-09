#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>

#include <algorithm>

#include "AudioSyncDsp.h"

namespace {

constexpr float PI = 3.14159265358979323846f;
constexpr size_t HOP_SAMPLES = 256;
constexpr uint32_t SAMPLE_RATE = 16000;
constexpr uint32_t HOP_MS = 16;

AudioSyncDspConfig fullConfig()
{
  AudioSyncDspConfig config;
  config.sampleRate = SAMPLE_RATE;
  config.sensitivity = 100;
  config.noiseGate = 15;
  config.smoothing = 20;
  config.fullBands = true;
  config.beatDetect = true;
  return config;
}

void process(AudioSyncDsp& dsp, const AudioSyncDspConfig& config, const int16_t* samples, uint32_t& nowMs)
{
  dsp.processFrame(samples, HOP_SAMPLES, nowMs, config);
  nowMs += HOP_MS;
}

void fillConstant(int16_t* samples, int16_t value)
{
  std::fill(samples, samples + HOP_SAMPLES, value);
}

void fillSine(int16_t* samples, float frequency, float amplitude, uint64_t firstSample)
{
  for (size_t i = 0; i < HOP_SAMPLES; ++i) {
    float phase = 2.0f * PI * frequency * static_cast<float>(firstSample + i) / static_cast<float>(SAMPLE_RATE);
    samples[i] = static_cast<int16_t>(sinf(phase) * amplitude);
  }
}

void fillTwoSines(int16_t* samples, float firstFrequency, float secondFrequency, float amplitude,
                  uint64_t firstSample)
{
  for (size_t i = 0; i < HOP_SAMPLES; ++i) {
    float time = static_cast<float>(firstSample + i) / static_cast<float>(SAMPLE_RATE);
    float value = sinf(2.0f * PI * firstFrequency * time) + sinf(2.0f * PI * secondFrequency * time);
    samples[i] = static_cast<int16_t>(value * amplitude * 0.5f);
  }
}

void primeSilence(AudioSyncDsp& dsp, const AudioSyncDspConfig& config, uint32_t& nowMs)
{
  int16_t samples[HOP_SAMPLES];
  fillConstant(samples, 900);
  for (int frame = 0; frame < 40; ++frame) process(dsp, config, samples, nowMs);
}

size_t dominantBand(const AudioSyncDspOutput& output)
{
  return static_cast<size_t>(std::max_element(output.bands, output.bands + AudioSyncDspOutput::BAND_COUNT) -
                             output.bands);
}

AudioSyncDspOutput analyzeTone(float frequency, float amplitude = 9000.0f)
{
  AudioSyncDsp dsp;
  AudioSyncDspConfig config = fullConfig();
  uint32_t nowMs = 0;
  uint64_t sampleIndex = 0;
  int16_t samples[HOP_SAMPLES];
  dsp.reset(nowMs);
  primeSilence(dsp, config, nowMs);
  sampleIndex = static_cast<uint64_t>(nowMs) * SAMPLE_RATE / 1000;
  for (int frame = 0; frame < 20; ++frame) {
    fillSine(samples, frequency, amplitude, sampleIndex);
    process(dsp, config, samples, nowMs);
    sampleIndex += HOP_SAMPLES;
  }
  return dsp.output(nowMs, config);
}

void testSilenceAndSignalSemantics()
{
  AudioSyncDsp dsp;
  AudioSyncDspConfig config = fullConfig();
  uint32_t nowMs = 0;
  int16_t samples[HOP_SAMPLES];
  dsp.reset(nowMs);
  primeSilence(dsp, config, nowMs);
  AudioSyncDspOutput silence = dsp.output(nowMs, config);
  assert(!silence.valid && !silence.rawGate && !silence.beatLocked && !silence.beat);
  assert(silence.energy == 0 && silence.bass == 0 && silence.mid == 0 && silence.treble == 0);
  assert(silence.confidence == 0 && silence.bpm == 0 && silence.beatMs == 0 && silence.phaseMs == 0);

  uint64_t sampleIndex = static_cast<uint64_t>(nowMs) * SAMPLE_RATE / 1000;
  for (int frame = 0; frame < 4; ++frame) {
    fillSine(samples, 120.0f, 9000.0f, sampleIndex);
    process(dsp, config, samples, nowMs);
    sampleIndex += HOP_SAMPLES;
  }
  AudioSyncDspOutput signal = dsp.output(nowMs, config);
  assert(signal.valid && signal.rawGate && signal.energy > 0 && signal.bass > 0);
  assert(!signal.beatLocked && signal.bpm == 0 && signal.beatMs == 0 && signal.phaseMs == 0);
}

void testFftBands()
{
  struct ToneCase {
    float frequency;
    size_t expectedBand;
  };
  const ToneCase cases[] = {
      {80.0f, 0}, {120.0f, 1}, {200.0f, 3}, {400.0f, 5},
      {1000.0f, 8}, {2000.0f, 10}, {4000.0f, 12}, {6000.0f, 14}};
  for (const ToneCase& tone : cases) {
    AudioSyncDspOutput output = analyzeTone(tone.frequency);
    size_t dominant = dominantBand(output);
    assert(output.valid && output.energy > 0);
    assert(dominant + 1 >= tone.expectedBand && dominant <= tone.expectedBand + 1);
    if (tone.frequency < 250.0f) assert(output.bass > output.mid && output.bass > output.treble);
    if (tone.frequency >= 400.0f && tone.frequency < 2000.0f) assert(output.mid > output.bass && output.mid > output.treble);
    if (tone.frequency >= 2000.0f) assert(output.treble > output.bass && output.treble > output.mid);
  }
}

void testCommonGainPreservesSpectralRelation()
{
  auto analyze = [](float amplitude) {
    AudioSyncDsp dsp;
    AudioSyncDspConfig config = fullConfig();
    uint32_t nowMs = 0;
    uint64_t sampleIndex = 0;
    int16_t samples[HOP_SAMPLES];
    dsp.reset(nowMs);
    primeSilence(dsp, config, nowMs);
    sampleIndex = static_cast<uint64_t>(nowMs) * SAMPLE_RATE / 1000;
    for (int frame = 0; frame < 80; ++frame) {
      fillTwoSines(samples, 120.0f, 1000.0f, amplitude, sampleIndex);
      process(dsp, config, samples, nowMs);
      sampleIndex += HOP_SAMPLES;
    }
    return dsp.output(nowMs, config);
  };
  AudioSyncDspOutput quiet = analyze(2500.0f);
  AudioSyncDspOutput loud = analyze(10000.0f);
  float quietRatio = static_cast<float>(quiet.bass + 1) / static_cast<float>(quiet.mid + 1);
  float loudRatio = static_cast<float>(loud.bass + 1) / static_cast<float>(loud.mid + 1);
  assert(fabsf(quietRatio - loudRatio) < 0.25f);
}

void testAudioActiveHangover()
{
  const uint32_t gaps[] = {100, 200, 300};
  for (uint32_t gapMs : gaps) {
    AudioSyncDsp dsp;
    AudioSyncDspConfig config = fullConfig();
    uint32_t nowMs = 0;
    uint64_t sampleIndex = 0;
    int16_t samples[HOP_SAMPLES];
    dsp.reset(nowMs);
    primeSilence(dsp, config, nowMs);
    sampleIndex = static_cast<uint64_t>(nowMs) * SAMPLE_RATE / 1000;
    for (int frame = 0; frame < 8; ++frame) {
      fillSine(samples, 400.0f, 6000.0f, sampleIndex);
      process(dsp, config, samples, nowMs);
      sampleIndex += HOP_SAMPLES;
    }
    fillConstant(samples, 0);
    for (uint32_t elapsed = 0; elapsed < gapMs; elapsed += HOP_MS) process(dsp, config, samples, nowMs);
    AudioSyncDspOutput gap = dsp.output(nowMs, config);
    assert(gap.valid && !gap.rawGate);
  }

  AudioSyncDsp dsp;
  AudioSyncDspConfig config = fullConfig();
  uint32_t nowMs = 0;
  uint64_t sampleIndex = 0;
  int16_t samples[HOP_SAMPLES];
  dsp.reset(nowMs);
  primeSilence(dsp, config, nowMs);
  sampleIndex = static_cast<uint64_t>(nowMs) * SAMPLE_RATE / 1000;
  for (int frame = 0; frame < 8; ++frame) {
    fillSine(samples, 400.0f, 6000.0f, sampleIndex);
    process(dsp, config, samples, nowMs);
    sampleIndex += HOP_SAMPLES;
  }
  fillConstant(samples, 0);
  for (int frame = 0; frame < 90; ++frame) process(dsp, config, samples, nowMs);
  AudioSyncDspOutput silence = dsp.output(nowMs, config);
  assert(!silence.valid && !silence.beatLocked && silence.energy == 0 && silence.bass == 0 && silence.mid == 0 &&
         silence.treble == 0 && silence.beatMs == 0 && silence.phaseMs == 0);
}

uint32_t pseudoNoise(uint32_t& state)
{
  state = state * 1664525U + 1013904223U;
  return state;
}

void testStationaryNoiseStaysSilent()
{
  AudioSyncDsp dsp;
  AudioSyncDspConfig config = fullConfig();
  uint32_t nowMs = 0;
  uint32_t noiseState = 23;
  int16_t samples[HOP_SAMPLES];
  dsp.reset(nowMs);
  for (int frame = 0; frame < 120; ++frame) {
    for (size_t i = 0; i < HOP_SAMPLES; ++i) {
      int32_t noise = static_cast<int32_t>((pseudoNoise(noiseState) >> 16) & 0xFFFFU) - 32768;
      samples[i] = static_cast<int16_t>(noise / 256);
    }
    process(dsp, config, samples, nowMs);
  }
  AudioSyncDspOutput output = dsp.output(nowMs, config);
  assert(!output.valid && !output.rawGate && !output.beatLocked && output.energy == 0);
}

void fillClickTrack(int16_t* samples, uint64_t firstSample, float bpm, uint32_t& noiseState, bool omitBeat,
                    bool falseTransient)
{
  uint32_t period = static_cast<uint32_t>(static_cast<float>(SAMPLE_RATE) * 60.0f / bpm + 0.5f);
  for (size_t i = 0; i < HOP_SAMPLES; ++i) {
    uint64_t sample = firstSample + i;
    uint32_t position = static_cast<uint32_t>(sample % period);
    bool click = position < 96 && !omitBeat;
    uint64_t falseOnset = static_cast<uint64_t>(SAMPLE_RATE) * 5U + SAMPLE_RATE / 4U;
    bool extra = falseTransient && sample >= falseOnset && sample < falseOnset + 96U;
    float background = sinf(2.0f * PI * 330.0f * static_cast<float>(sample) / SAMPLE_RATE) * 350.0f;
    int32_t noise = static_cast<int32_t>((pseudoNoise(noiseState) >> 16) & 0xFFFFU) - 32768;
    float pulse = (click || extra) ? static_cast<float>(noise) * 0.42f : 0.0f;
    samples[i] = static_cast<int16_t>(background + pulse);
  }
}

AudioSyncDspOutput runClickTrack(float bpm, uint32_t seconds, bool dropBeats = false, bool falseTransient = false)
{
  AudioSyncDsp dsp;
  AudioSyncDspConfig config = fullConfig();
  uint32_t nowMs = 0;
  uint64_t sampleIndex = 0;
  uint32_t noiseState = 1;
  int16_t samples[HOP_SAMPLES];
  dsp.reset(nowMs);
  primeSilence(dsp, config, nowMs);
  sampleIndex = static_cast<uint64_t>(nowMs) * SAMPLE_RATE / 1000;
  uint32_t frames = seconds * 1000U / HOP_MS;
  uint32_t periodSamples = static_cast<uint32_t>(static_cast<float>(SAMPLE_RATE) * 60.0f / bpm + 0.5f);
  for (uint32_t frame = 0; frame < frames; ++frame) {
    uint32_t beatNumber = static_cast<uint32_t>(sampleIndex / periodSamples);
    bool omit = dropBeats && (beatNumber == 5 || beatNumber == 11);
    fillClickTrack(samples, sampleIndex, bpm, noiseState, omit, falseTransient);
    process(dsp, config, samples, nowMs);
    sampleIndex += HOP_SAMPLES;
  }
  return dsp.output(nowMs, config);
}

void assertTempo(float bpm, const AudioSyncDspOutput& output)
{
  if (fabsf(static_cast<float>(output.bpm) - bpm) > 5.0f) {
    fprintf(stderr, "tempo mismatch expected=%.1f actual=%u candidate=%.1f confidence=%u locked=%d\n", bpm,
            output.bpm, output.bestTempoBpm, output.confidence, output.beatLocked);
  }
  assert(output.valid && output.beatLocked);
  assert(fabsf(static_cast<float>(output.bpm) - bpm) <= 5.0f);
  assert(output.confidence >= 40);
}

void testBeatTemposAndRobustness()
{
  const float tempos[] = {60.0f, 90.0f, 120.0f, 150.0f, 180.0f};
  for (float bpm : tempos) assertTempo(bpm, runClickTrack(bpm, 14));
  assertTempo(120.0f, runClickTrack(120.0f, 14, true, false));
  assertTempo(120.0f, runClickTrack(120.0f, 14, false, true));
}

void testBeatLockSurvivesShortGap()
{
  AudioSyncDsp dsp;
  AudioSyncDspConfig config = fullConfig();
  uint32_t nowMs = 0;
  uint64_t sampleIndex = 0;
  uint32_t noiseState = 11;
  int16_t samples[HOP_SAMPLES];
  dsp.reset(nowMs);
  primeSilence(dsp, config, nowMs);
  sampleIndex = static_cast<uint64_t>(nowMs) * SAMPLE_RATE / 1000;
  for (uint32_t frame = 0; frame < 10U * 1000U / HOP_MS; ++frame) {
    fillClickTrack(samples, sampleIndex, 120.0f, noiseState, false, false);
    process(dsp, config, samples, nowMs);
    sampleIndex += HOP_SAMPLES;
  }
  assertTempo(120.0f, dsp.output(nowMs, config));
  fillConstant(samples, 0);
  for (int frame = 0; frame < 19; ++frame) process(dsp, config, samples, nowMs);
  AudioSyncDspOutput gap = dsp.output(nowMs, config);
  assert(gap.valid && !gap.rawGate && gap.beatLocked && gap.beatMs > 0);
}

void testTempoChangeAndSilenceUnlock()
{
  AudioSyncDsp dsp;
  AudioSyncDspConfig config = fullConfig();
  uint32_t nowMs = 0;
  uint64_t sampleIndex = 0;
  uint32_t noiseState = 7;
  int16_t samples[HOP_SAMPLES];
  dsp.reset(nowMs);
  primeSilence(dsp, config, nowMs);
  sampleIndex = static_cast<uint64_t>(nowMs) * SAMPLE_RATE / 1000;
  for (uint32_t frame = 0; frame < 8U * 1000U / HOP_MS; ++frame) {
    fillClickTrack(samples, sampleIndex, 120.0f, noiseState, false, false);
    process(dsp, config, samples, nowMs);
    sampleIndex += HOP_SAMPLES;
  }
  assertTempo(120.0f, dsp.output(nowMs, config));

  uint64_t changedSample = 0;
  for (uint32_t frame = 0; frame < 12U * 1000U / HOP_MS; ++frame) {
    fillClickTrack(samples, changedSample, 150.0f, noiseState, false, false);
    process(dsp, config, samples, nowMs);
    changedSample += HOP_SAMPLES;
  }
  assertTempo(150.0f, dsp.output(nowMs, config));

  fillConstant(samples, 0);
  for (int frame = 0; frame < 90; ++frame) process(dsp, config, samples, nowMs);
  AudioSyncDspOutput silence = dsp.output(nowMs, config);
  assert(!silence.valid && !silence.beatLocked && silence.beatMs == 0 && silence.phaseMs == 0);
}

void testNoBeatForStationaryTone()
{
  AudioSyncDspOutput output = analyzeTone(1000.0f);
  assert(output.valid && !output.beatLocked && output.beatMs == 0 && output.phaseMs == 0);
}

}  // namespace

int main()
{
  testSilenceAndSignalSemantics();
  testFftBands();
  testCommonGainPreservesSpectralRelation();
  testAudioActiveHangover();
  testStationaryNoiseStaysSilent();
  testBeatTemposAndRobustness();
  testBeatLockSurvivesShortGap();
  testTempoChangeAndSilenceUnlock();
  testNoBeatForStationaryTone();
  return 0;
}
