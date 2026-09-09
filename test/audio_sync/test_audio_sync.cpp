#include <assert.h>
#include <math.h>
#include <stdint.h>

#include "AudioSyncDsp.h"

namespace {

constexpr float PI = 3.14159265358979323846f;
constexpr size_t FRAME_SAMPLES = 256;
constexpr uint32_t SAMPLE_RATE = 8000;

void fillConstant(int16_t* samples, int16_t value)
{
  for (size_t i = 0; i < FRAME_SAMPLES; ++i) {
    samples[i] = value;
  }
}

void fillSine(int16_t* samples, float frequency, float amplitude, uint32_t firstSample)
{
  for (size_t i = 0; i < FRAME_SAMPLES; ++i) {
    float phase = 2.0f * PI * frequency * static_cast<float>(firstSample + i) / static_cast<float>(SAMPLE_RATE);
    samples[i] = static_cast<int16_t>(sinf(phase) * amplitude);
  }
}

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

void primeSilence(AudioSyncDsp& dsp, const AudioSyncDspConfig& config, uint32_t& nowMs, uint32_t& sampleIndex)
{
  int16_t samples[FRAME_SAMPLES];
  fillConstant(samples, 900);
  for (int frame = 0; frame < 12; ++frame) {
    dsp.processFrame(samples, FRAME_SAMPLES, nowMs, config);
    nowMs += 32;
    sampleIndex += FRAME_SAMPLES;
  }
}

void testDcRemovalAndSilence()
{
  AudioSyncDsp dsp;
  AudioSyncDspConfig config = fullConfig();
  uint32_t nowMs = 0;
  uint32_t sampleIndex = 0;
  dsp.reset(nowMs);
  primeSilence(dsp, config, nowMs, sampleIndex);
  AudioSyncDspOutput output = dsp.output(nowMs, config);
  assert(!output.valid && !output.beatLocked && !output.beat);
  assert(output.rms < 0.1f);
  assert(output.energy == 0 && output.bass == 0 && output.mid == 0 && output.treble == 0);
  assert(output.confidence == 0 && output.bpm == 0 && output.beatMs == 0 && output.phaseMs == 0);
}

void testSignalWithoutBeatLock()
{
  AudioSyncDsp dsp;
  AudioSyncDspConfig config = fullConfig();
  int16_t samples[FRAME_SAMPLES];
  uint32_t nowMs = 0;
  uint32_t sampleIndex = 0;
  dsp.reset(nowMs);
  primeSilence(dsp, config, nowMs, sampleIndex);
  fillSine(samples, 120.0f, 12000.0f, sampleIndex);
  dsp.processFrame(samples, FRAME_SAMPLES, nowMs, config);
  AudioSyncDspOutput output = dsp.output(nowMs, config);
  assert(output.valid && output.energy > 0 && output.bass > 0);
  assert(!output.beatLocked && output.bpm == 0 && output.beatMs == 0 && output.phaseMs == 0);
}

void testBandSeparation()
{
  AudioSyncDspConfig config = fullConfig();
  int16_t samples[FRAME_SAMPLES];

  AudioSyncDsp bassDsp;
  uint32_t bassNow = 0;
  uint32_t bassSample = 0;
  bassDsp.reset(bassNow);
  primeSilence(bassDsp, config, bassNow, bassSample);
  for (int frame = 0; frame < 8; ++frame) {
    fillSine(samples, 120.0f, 12000.0f, bassSample);
    bassDsp.processFrame(samples, FRAME_SAMPLES, bassNow, config);
    bassNow += 32;
    bassSample += FRAME_SAMPLES;
  }
  AudioSyncDspOutput bass = bassDsp.output(bassNow, config);
  assert(bass.energy > 80);
  assert(bass.bass > bass.mid);
  assert(bass.bass > bass.treble);

  AudioSyncDsp trebleDsp;
  uint32_t trebleNow = 0;
  uint32_t trebleSample = 0;
  trebleDsp.reset(trebleNow);
  primeSilence(trebleDsp, config, trebleNow, trebleSample);
  for (int frame = 0; frame < 8; ++frame) {
    fillSine(samples, 2800.0f, 12000.0f, trebleSample);
    trebleDsp.processFrame(samples, FRAME_SAMPLES, trebleNow, config);
    trebleNow += 32;
    trebleSample += FRAME_SAMPLES;
  }
  AudioSyncDspOutput treble = trebleDsp.output(trebleNow, config);
  assert(treble.energy > 80);
  assert(treble.treble > treble.bass);
  assert(treble.treble > treble.mid);
}

void testBeatTracking()
{
  AudioSyncDsp dsp;
  AudioSyncDspConfig config = fullConfig();
  int16_t samples[FRAME_SAMPLES];
  uint32_t nowMs = 0;
  uint32_t sampleIndex = 0;
  dsp.reset(nowMs);
  primeSilence(dsp, config, nowMs, sampleIndex);

  for (int frame = 0; frame < 110; ++frame) {
    bool pulse = (frame % 16) == 0;
    fillSine(samples, 120.0f, pulse ? 14000.0f : 350.0f, sampleIndex);
    dsp.processFrame(samples, FRAME_SAMPLES, nowMs, config);
    nowMs += 32;
    sampleIndex += FRAME_SAMPLES;
  }

  AudioSyncDspOutput output = dsp.output(nowMs, config);
  assert(output.valid && output.beatLocked);
  assert(output.bpm >= 112 && output.bpm <= 128);
  assert(output.beatMs >= 468 && output.beatMs <= 536);
  assert(output.confidence > 20);
}

}  // namespace

int main()
{
  testDcRemovalAndSilence();
  testSignalWithoutBeatLock();
  testBandSeparation();
  testBeatTracking();
  return 0;
}
