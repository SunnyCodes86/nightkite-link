#include "CardputerAudioSync.h"

#include "M5Cardputer.h"
#include "SoundManager.h"

bool CardputerAudioSync::begin(SoundManager& sound)
{
  if (running) {
    return true;
  }

  soundManager = &sound;
  error = false;
  currentFrame = 0;
  frameQueued = false;
  framesProcessed = 0;
  captureDrops = 0;
  captureStartedMs = 0;
  nextCaptureCheckMs = 0;
  lastFrameCompletedMs = 0;
  dsp.reset(millis());

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
  captureStartedMs = millis();
  constexpr uint32_t frameMs = (FRAME_SAMPLES * 1000UL) / SAMPLE_RATE;
  nextCaptureCheckMs = captureStartedMs + frameMs;
  if (!queueFrame(0) || !queueFrame(1)) {
    fail("mic queue failed");
    return false;
  }
  frameQueued = true;

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
  if (soundManager != nullptr) {
    soundManager->setCaptureActive(false);
  }
}

void CardputerAudioSync::tick(const AudioSyncDspConfig& config)
{
  uint32_t nowMs = millis();
  constexpr uint32_t frameMs = (FRAME_SAMPLES * 1000UL) / SAMPLE_RATE;
  if (!running || !frameQueued || static_cast<int32_t>(nowMs - nextCaptureCheckMs) < 0) return;
  size_t pending = M5Cardputer.Mic.isRecording();
  if (pending >= 2) return;
  size_t completed = 2 - pending;
  if (completed == 2 && lastFrameCompletedMs != 0) {
    uint32_t elapsed = nowMs - lastFrameCompletedMs;
    uint32_t bufferedMs = 2 * frameMs;
    if (elapsed > bufferedMs + frameMs / 2) {
      captureDrops += (elapsed - bufferedMs + frameMs / 2) / frameMs;
    }
  }
  uint8_t completedFrames[2] = {};
  for (size_t frame = 0; frame < completed; ++frame) {
    completedFrames[frame] = (currentFrame + frame) & 1;
    uint32_t frameAt = nowMs - static_cast<uint32_t>((completed - frame - 1) * frameMs);
    lastFrameCompletedMs = frameAt;
    dsp.processFrame(frames[completedFrames[frame]], FRAME_SAMPLES, frameAt, config);
    ++framesProcessed;
  }
  currentFrame = (currentFrame + completed) & 1;
  for (size_t frame = 0; frame < completed; ++frame) {
    if (!queueFrame(completedFrames[frame])) {
      fail("mic queue failed");
      return;
    }
  }
  nextCaptureCheckMs = nowMs + (completed == 2 ? frameMs : 1);
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

unsigned long CardputerAudioSync::captureDropCount() const
{
  return captureDrops;
}

bool CardputerAudioSync::queueFrame(uint8_t frame)
{
  return M5Cardputer.Mic.record(frames[frame], FRAME_SAMPLES, SAMPLE_RATE, false);
}

void CardputerAudioSync::fail(const char* reason)
{
  error = true;
  Serial.print("audio: mic error=");
  Serial.println(reason);
  end();
}
