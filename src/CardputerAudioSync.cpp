#include "CardputerAudioSync.h"

#include "M5Cardputer.h"
#include "SoundManager.h"

bool CardputerAudioSync::begin(SoundManager& sound, uint16_t fallbackBpm)
{
  if (running) {
    return true;
  }

  soundManager = &sound;
  error = false;
  currentFrame = 0;
  frameQueued = false;
  framesProcessed = 0;
  previousBeat = false;
  lastDebugMs = 0;
  dsp.reset(millis(), fallbackBpm);

  auto config = M5Cardputer.Mic.config();
  config.sample_rate = SAMPLE_RATE;
  config.over_sampling = 1;
  config.noise_filter_level = 0;
  config.magnification = config.use_adc ? 16 : 1;
  config.dma_buf_len = FRAME_SAMPLES;
  config.dma_buf_count = 4;
  M5Cardputer.Mic.config(config);
  if (!M5Cardputer.Mic.isEnabled()) {
    fail("mic unavailable");
    return false;
  }

  soundManager->setCaptureActive(true);
  if (!M5Cardputer.Mic.begin()) {
    fail("mic begin failed");
    return false;
  }

  running = true;
  if (!queueCurrentFrame()) {
    fail("mic queue failed");
    return false;
  }

  Serial.print("audio: mic init ok sample_rate=");
  Serial.print(SAMPLE_RATE);
  Serial.print(" frame_samples=");
  Serial.print(FRAME_SAMPLES);
  Serial.print(" frame_ms=");
  Serial.println((FRAME_SAMPLES * 1000UL) / SAMPLE_RATE);
  return true;
}

void CardputerAudioSync::end()
{
  if (running || M5Cardputer.Mic.isRunning()) {
    M5Cardputer.Mic.end();
  }
  running = false;
  frameQueued = false;
  previousBeat = false;
  if (soundManager != nullptr) {
    soundManager->setCaptureActive(false);
  }
}

void CardputerAudioSync::tick(const AudioSyncDspConfig& config)
{
  if (!running || !frameQueued || M5Cardputer.Mic.isRecording() != 0) {
    return;
  }

  uint8_t completedFrame = currentFrame;
  currentFrame ^= 1;
  frameQueued = false;
  if (!queueCurrentFrame()) {
    fail("mic queue failed");
    return;
  }

  uint32_t nowMs = millis();
  dsp.processFrame(frames[completedFrame], FRAME_SAMPLES, nowMs, config);
  ++framesProcessed;
  printPeriodicDebug(config, nowMs);
}

void CardputerAudioSync::tapTempo(uint16_t bpm, uint32_t nowMs)
{
  dsp.tapTempo(bpm, nowMs);
}

bool CardputerAudioSync::active() const
{
  return running;
}

bool CardputerAudioSync::failed() const
{
  return error;
}

AudioSyncDspOutput CardputerAudioSync::output(uint32_t nowMs, const AudioSyncDspConfig& config) const
{
  return dsp.output(nowMs, config);
}

unsigned long CardputerAudioSync::frameCount() const
{
  return framesProcessed;
}

bool CardputerAudioSync::queueCurrentFrame()
{
  frameQueued = M5Cardputer.Mic.record(frames[currentFrame], FRAME_SAMPLES, SAMPLE_RATE, false);
  return frameQueued;
}

void CardputerAudioSync::fail(const char* reason)
{
  error = true;
  Serial.print("audio: mic error=");
  Serial.println(reason);
  end();
}

void CardputerAudioSync::printPeriodicDebug(const AudioSyncDspConfig& config, uint32_t nowMs)
{
  AudioSyncDspOutput current = dsp.output(nowMs, config);
  if (current.beat && !previousBeat) {
    Serial.print("audio: beat bpm=");
    Serial.print(current.bpm);
    Serial.print(" beat_ms=");
    Serial.print(current.beatMs);
    Serial.print(" phase=");
    Serial.print(current.phaseMs);
    Serial.print(" confidence=");
    Serial.println(current.confidence);
  }
  previousBeat = current.beat;

  if (lastDebugMs != 0 && nowMs - lastDebugMs < 1000) {
    return;
  }
  lastDebugMs = nowMs;
  Serial.print("audio: frame=");
  Serial.print(framesProcessed);
  Serial.print(" rms=");
  Serial.print(current.rms, 1);
  Serial.print(" peak=");
  Serial.print(current.peak, 1);
  Serial.print(" noise=");
  Serial.print(current.noiseFloor, 1);
  Serial.print(" energy=");
  Serial.print(current.energy);
  Serial.print(" bass=");
  Serial.print(current.bass);
  Serial.print(" mid=");
  Serial.print(current.mid);
  Serial.print(" treble=");
  Serial.print(current.treble);
  Serial.print(" confidence=");
  Serial.print(current.confidence);
  Serial.print(" bpm=");
  Serial.print(current.bpm);
  Serial.print(" beat_ms=");
  Serial.print(current.beatMs);
  Serial.print(" phase=");
  Serial.println(current.phaseMs);
}
