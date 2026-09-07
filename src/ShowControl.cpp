#include "ShowControl.h"
#include "SyncBeaconCodec.h"
#include <string.h>

namespace NightKiteShow {
int32_t delta(uint32_t a, uint32_t b) {
  const uint32_t d = a - b;
  return d <= INT32_MAX ? int32_t(d) : -1 - int32_t(UINT32_MAX - d);
}
const char* validate(const Event& e) {
  if ((e.target.kind == TargetKind::All && e.target.value) ||
      (e.target.kind == TargetKind::Group && (!e.target.value || e.target.value > 255)) ||
      (e.target.kind == TargetKind::Single && e.target.value > 0xFFFFFF) || uint8_t(e.target.kind) > 2) return "target";
  uint8_t used = 0;
  switch (e.command) {
    case Command::Pattern: if (!e.params[0] || e.params[0] > 27) return "pattern"; used = 1; break;
    case Command::Brightness: if (!e.params[0]) return "brightness"; used = 1; break;
    case Command::Solid: used = 4; break;
    case Command::Blackout: case Command::Release: break;
    case Command::Clear: if (e.params[1] > 32) return "segments"; used = 2; break;
    case Command::Segment:
      if (e.params[1] >= 32 || !e.params[3] || uint16_t(e.params[2]) + e.params[3] > 70) return "segment";
      used = 7; break;
    case Command::Apply: used = 1; break;
    case Command::Clock: if (e.target.kind != TargetKind::All) return "target"; break;
    default: return "command";
  }
  for (uint8_t i = used; i < 7; ++i) if (e.params[i]) return "parameters";
  return nullptr;
}
static void put(uint8_t* p, uint32_t value, unsigned n) { for (unsigned i = 0; i < n; ++i) p[i] = value >> (8 * i); }
static uint32_t get(const uint8_t* p, unsigned n) { uint32_t v = 0; for (unsigned i = 0; i < n; ++i) v |= uint32_t(p[i]) << (8 * i); return v; }
bool encode(const Packet& p, uint8_t* bytes, size_t size) {
  const int32_t ahead = delta(p.event.executeAt, p.senderMs);
  if (!bytes || size < ADV_SIZE || validate(p.event) || ahead < -250 || ahead > 30000 ||
      (p.event.command == Command::Clock && (p.id || ahead))) return false;
  const uint8_t header[] = {2, 1, 6, 27, 255, 255, 255};
  memcpy(bytes, header, 7);
  uint8_t* b = bytes + 7;
  memset(b, 0, PACKET_SIZE);
  b[0] = 'N'; b[1] = 'S'; b[2] = 1; b[3] = uint8_t(p.event.target.kind) << 6 | uint8_t(p.event.command);
  put(b + 4, p.event.target.value, 3); put(b + 7, p.id, 2); put(b + 9, p.senderMs, 4);
  put(b + 13, p.event.executeAt, 2); memcpy(b + 15, p.event.params, 7);
  put(b + 22, NightKiteSync::crc16Ccitt(b, PACKET_SIZE), 2);
  return true;
}
bool decode(const uint8_t* b, size_t size, Packet& p) {
  if (!b || size != PACKET_SIZE || b[0] != 'N' || b[1] != 'S' || b[2] != 1) return false;
  uint8_t copy[PACKET_SIZE]; memcpy(copy, b, size); copy[22] = copy[23] = 0;
  if (NightKiteSync::crc16Ccitt(copy, size) != get(b + 22, 2)) return false;
  Packet candidate;
  candidate.event.target.kind = TargetKind(b[3] >> 6); candidate.event.target.value = get(b + 4, 3);
  candidate.event.command = Command(b[3] & 63); candidate.id = get(b + 7, 2); candidate.senderMs = get(b + 9, 4);
  const uint16_t d = uint16_t(get(b + 13, 2)) - uint16_t(candidate.senderMs);
  candidate.event.executeAt = candidate.senderMs + (d <= INT16_MAX ? int32_t(d) : int32_t(d) - 65536);
  memcpy(candidate.event.params, b + 15, 7);
  uint8_t adv[ADV_SIZE]; if (!encode(candidate, adv, sizeof(adv))) return false;
  p = candidate; return true;
}
const char* commandName(Command c) {
  switch (c) {
    case Command::Pattern: return "PATTERN"; case Command::Brightness: return "BRIGHTNESS";
    case Command::Solid: return "SOLID"; case Command::Blackout: return "BLACKOUT";
    case Command::Release: return "RELEASE"; case Command::Clear: return "CLEAR";
    case Command::Segment: return "SEGMENT"; case Command::Apply: return "APPLY";
    case Command::Clock: return "CLOCK";
  }
  return "UNKNOWN";
}
void Engine::arm(uint32_t now, uint16_t randomId) {
  if (active()) return;
  count = 0; nextId = randomId; armAt = now; warmupClocks = 0;
  haveSlot = haveAudio = haveClock = haveRemote = false; mode = State::Arming;
}
void Engine::stop(uint32_t now, bool disarm) {
  if (mode == State::Stopping) { disarmAfterStop = disarmAfterStop || disarm; return; }
  if (!active()) return;
  count = 1; queue[0] = Entry{}; queue[0].packet.id = nextId++;
  queue[0].packet.event.command = Command::Release;
  stopAt = now + LIVE_LEAD_MS;
  if (haveRemote && delta(lastRemoteDue + 100, stopAt) > 0) stopAt = lastRemoteDue + 100;
  queue[0].packet.event.executeAt = stopAt;
  disarmAfterStop = disarm; mode = State::Stopping;
}
const char* Engine::enqueue(const Event& e, uint32_t now, uint16_t& id) {
  const char* error = validate(e);
  if (!error && e.command == Command::Clock) error = "reserved";
  if (!error && !ready()) error = "not_ready";
  if (!error && (delta(e.executeAt, now) < int32_t(MIN_LEAD_MS) || delta(e.executeAt, now) > int32_t(MAX_TIMELINE_MS))) error = "lead";
  if (!error && count == QUEUE_SIZE) error = "queue_full";
  // Conservative shared-radio budget, regardless of target. Three events in any
  // 1200 ms window leave airtime for three copies, audio and CLOCK. No eviction.
  unsigned nearby = 0;
  for (uint8_t i = 0; i < count; ++i) {
    const int32_t d = delta(e.executeAt, queue[i].packet.event.executeAt);
    if (d > -int32_t(LOOKAHEAD_MS) && d < int32_t(LOOKAHEAD_MS)) ++nearby;
  }
  if (!error && nearby >= 3) error = "density";
  if (error) { ++counters.rejected; return error; }
  Entry entry; entry.packet.event = e; entry.packet.id = nextId++;
  uint8_t i = count;
  while (i && delta(e.executeAt, queue[i - 1].packet.event.executeAt) < 0) { queue[i] = queue[i - 1]; --i; }
  queue[i] = entry; ++count; id = entry.packet.id; return nullptr;
}
void Engine::retire(uint32_t now) {
  while (count && delta(now, queue[0].packet.event.executeAt) > 250) {
    if (queue[0].repeats < REPEATS) ++counters.missed;
    if (mode == State::Stopping && !queue[0].repeats) mode = State::Error;
    for (uint8_t i = 1; i < count; ++i) queue[i - 1] = queue[i];
    --count;
  }
  if (mode == State::Stopping && delta(now, stopAt) > 250) {
    mode = disarmAfterStop ? State::Off : State::Ready;
  }
}
Transmission Engine::next(uint32_t now, bool audio) {
  retire(now);
  if (!active()) return {};
  if (mode == State::Arming && haveClock && now - lastClock > 300) { armAt = now; warmupClocks = 0; }
  if (mode == State::Arming && warmupClocks >= 15 && now - armAt >= ARM_MS) mode = State::Ready;
  if (haveSlot && now - lastSlot < SLOT_MS) return {};
  Transmission tx;
  if (audio && (!haveAudio || now - lastAudio >= AUDIO_MS)) tx.kind = TxKind::Audio;
  else if (!haveClock || now - lastClock >= CLOCK_MS) {
    tx.kind = TxKind::Clock; tx.packet.event.command = Command::Clock; tx.packet.event.executeAt = now;
  } else if (mode != State::Arming) {
    unsigned remote = 0;
    for (uint8_t i = 0; i < count; ++i) if (queue[i].repeats && delta(queue[i].packet.event.executeAt + 250, now) >= 0) ++remote;
    for (uint8_t i = 0; i < count; ++i) {
      Entry& e = queue[i]; const int32_t ahead = delta(e.packet.event.executeAt, now);
      if (ahead > int32_t(LOOKAHEAD_MS)) break;
      if (ahead < int32_t(LAST_SEND_LEAD_MS) || e.repeats >= REPEATS ||
          (e.repeats && now - e.lastTx < RETRY_MS) || (!e.repeats && remote >= RECEIVER_BUDGET)) continue;
      tx.kind = TxKind::Event; tx.packet = e.packet; break;
    }
  }
  tx.packet.senderMs = now;
  return tx;
}
void Engine::sent(const Transmission& tx, uint32_t now, bool ok) {
  if (tx.kind == TxKind::None) return;
  lastSlot = now; haveSlot = true;
  if (!ok) { ++counters.radioErrors; return; }
  counters.lastSentMs = now;
  if (tx.kind == TxKind::Audio) {
    if (haveAudio && now - lastAudio > counters.maxAudioGap) counters.maxAudioGap = now - lastAudio;
    haveAudio = true; lastAudio = now; ++counters.audio;
  } else if (tx.kind == TxKind::Clock) {
    if (haveClock && now - lastClock > counters.maxClockGap) counters.maxClockGap = now - lastClock;
    haveClock = true; lastClock = now; ++counters.clock; ++warmupClocks;
  } else {
    for (uint8_t i = 0; i < count; ++i) if (queue[i].packet.id == tx.packet.id) {
      ++queue[i].repeats; queue[i].lastTx = now; break;
    }
    ++counters.events; counters.lastId = tx.packet.id; counters.lastExecuteAt = tx.packet.event.executeAt;
    if (!haveRemote || delta(tx.packet.event.executeAt, lastRemoteDue) > 0) lastRemoteDue = tx.packet.event.executeAt;
    haveRemote = true;
  }
}
const char* Engine::stateName() const {
  switch (mode) {
    case State::Off: return "OFF"; case State::Arming: return "ARMING"; case State::Ready: return "READY";
    case State::Playing: return "PLAYING"; case State::Stopping: return "STOPPING"; case State::Error: return "ERROR";
  }
  return "ERROR";
}
} // namespace NightKiteShow
