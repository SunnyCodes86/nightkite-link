#pragma once
#include <stddef.h>
#include <stdint.h>

namespace NightKiteShow {
constexpr size_t PACKET_SIZE = 24, ADV_SIZE = 31;
constexpr uint32_t ARM_MS = 3000, SLOT_MS = 40, AUDIO_MS = 120, CLOCK_MS = 200;
constexpr uint32_t LOOKAHEAD_MS = 1200, LIVE_LEAD_MS = 1000, MIN_LEAD_MS = 800;
constexpr uint32_t RETRY_MS = 80, LAST_SEND_LEAD_MS = 40;
constexpr uint8_t REPEATS = 3, QUEUE_SIZE = 32, RECEIVER_BUDGET = 6;
constexpr uint32_t MAX_TIMELINE_MS = 24UL * 60 * 60 * 1000;
constexpr size_t LINE_SIZE = 224;

enum class TargetKind : uint8_t { All, Group, Single };
struct Target { TargetKind kind = TargetKind::All; uint32_t value = 0; };
enum class Command : uint8_t {
  Pattern = 1, Brightness, Solid, Blackout, Release, Clear, Segment, Apply, Clock
};
struct Event {
  Target target;
  Command command = Command::Blackout;
  uint8_t params[7] = {};
  uint32_t executeAt = 0;
};
struct Packet { Event event; uint16_t id = 0; uint32_t senderMs = 0; };
int32_t delta(uint32_t a, uint32_t b);
const char* validate(const Event& event);
bool encode(const Packet& packet, uint8_t* bytes, size_t capacity);
bool decode(const uint8_t* bytes, size_t size, Packet& packet);
const char* commandName(Command command);

enum class State : uint8_t { Off, Arming, Ready, Playing, Stopping, Error };
enum class TxKind : uint8_t { None, Audio, Clock, Event };
struct Transmission {
  TxKind kind = TxKind::None;
  Packet packet;
};
struct Counters {
  uint32_t audio = 0, clock = 0, events = 0, rejected = 0, missed = 0, radioErrors = 0;
  uint32_t maxAudioGap = 0, maxClockGap = 0, lastSentMs = 0, lastExecuteAt = 0;
  uint16_t lastId = 0;
};
class Engine {
public:
  void arm(uint32_t now, uint16_t randomId);
  void stop(uint32_t now, bool disarm);
  const char* enqueue(const Event& event, uint32_t now, uint16_t& id);
  Transmission next(uint32_t now, bool audio);
  void sent(const Transmission& tx, uint32_t now, bool ok);
  void setPlaying(bool value) { if (ready()) mode = value ? State::Playing : State::Ready; }
  State state() const { return mode; }
  bool active() const { return mode != State::Off && mode != State::Error; }
  bool ready() const { return mode == State::Ready || mode == State::Playing; }
  uint8_t depth() const { return count; }
  uint32_t nextTime() const { return count ? queue[0].packet.event.executeAt : 0; }
  const char* stateName() const;
  Counters counters;
private:
  struct Entry { Packet packet; uint8_t repeats = 0; uint32_t lastTx = 0; };
  Entry queue[QUEUE_SIZE];
  uint8_t count = 0;
  uint16_t nextId = 0;
  State mode = State::Off;
  uint32_t armAt = 0, lastSlot = 0, lastAudio = 0, lastClock = 0, lastRemoteDue = 0, stopAt = 0;
  uint16_t warmupClocks = 0;
  bool haveSlot = false, haveAudio = false, haveClock = false, haveRemote = false, disarmAfterStop = false;
  void retire(uint32_t now);
};
} // namespace NightKiteShow
