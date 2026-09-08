#include "ShowRuntime.h"
#include <esp_random.h>
#include <string.h>
using namespace NightKiteShow;
void ShowRuntime::begin(fs::FS& storage, Hardware hardware) { sd = &storage; hw = hardware; }
bool ShowRuntime::audioActive(NightKiteSync::BeaconInput* input) const {
  NightKiteSync::BeaconInput ignored;
  return hw.audio && hw.audio(input ? *input : ignored);
}
bool ShowRuntime::validFilename(const char* name) {
  const size_t n = strlen(name);
  if (n < 5 || n > 40 || strcmp(name + n - 4, ".nks")) return false;
  for (size_t i = 0; i < n - 4; ++i) {
    const char c = name[i];
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_')) return false;
  }
  return true;
}
int ShowRuntime::readFile(void* context) {
  File& f = *static_cast<File*>(context);
  if (!f) return -2;
  if (f.position() >= f.size()) return -1;
  const int c = f.read(); return c < 0 ? -2 : c;
}
bool ShowRuntime::rewindFile(void* context) { return static_cast<File*>(context)->seek(0); }
const char* ShowRuntime::arm() {
  if (engine.active()) return nullptr;
  if (upload || player.state() == PlayerState::Validating) return "busy";
  if (!hw.arm || !hw.arm()) return lastError = "radio_busy";
  radioOwned = true; engine.arm(millis(), uint16_t(esp_random())); return nullptr;
}
void ShowRuntime::stop(bool disarm) { player.stop(engine, millis()); if (disarm) engine.stop(millis(), true); }
const char* ShowRuntime::live(Event event, uint16_t& id) {
  if (player.state() == PlayerState::Playing || (player.state() == PlayerState::Validating && engine.active())) return "player_busy";
  if (!event.executeAt) event.executeAt = millis() + LIVE_LEAD_MS;
  engine.setAudioActive(audioActive());
  return engine.enqueue(event, millis(), id);
}
const char* ShowRuntime::load(const char* name) {
  if (engine.active() || upload) return "busy";
  if (!validFilename(name)) return "filename";
  if (!hw.storage || !hw.storage()) return "sd";
  char path[64]; snprintf(path, sizeof(path), "/shows/%s", name);
  File candidate = sd->open(path, FILE_READ);
  if (!candidate || candidate.isDirectory()) return "file";
  file.close(); file = candidate;
  Reader reader; reader.context = &file; reader.read = readFile; reader.rewind = rewindFile;
  player.load(reader, audioActive()); return nullptr;
}
const char* ShowRuntime::play() { engine.setAudioActive(audioActive()); return player.play(engine, millis()); }
void ShowRuntime::list(char* result, size_t size) {
  result[0] = 0;
  if (engine.active()) { snprintf(result, size, "busy"); return; }
  if (!hw.storage || !hw.storage()) { snprintf(result, size, "sd_unavailable"); return; }
  sd->mkdir("/shows"); File directory = sd->open("/shows");
  for (unsigned i = 0; i < 64 && directory; ++i) {
    File entry = directory.openNextFile(); if (!entry) break;
    const char* name = strrchr(entry.name(), '/'); name = name ? name + 1 : entry.name();
    if (!entry.isDirectory() && validFilename(name)) {
      const size_t used = strlen(result);
      if (used + strlen(name) + 2 >= size) break;
      snprintf(result + used, size - used, "%s%s", used ? "," : "", name);
    }
    entry.close();
  }
}
const char* ShowRuntime::hostRequest(const char* text) {
  bridgeSeen = true; request(text); return reply;
}
void ShowRuntime::request(const char* text) {
  const uint32_t now = millis(); Request r;
  const char* error = parseRequest(text, now, r);
  const char* cached = nullptr; const int found = cache.find(r.id, text, cached);
  if (!error && found == 1) { snprintf(reply, sizeof(reply), "%s", cached); return; }
  if (!error && found < 0) error = "request_conflict";
  char fields[384] = {}; uint16_t id = 0;
  if (!error) {
    switch (r.operation) {
      case Operation::Hello: snprintf(fields, sizeof(fields), "api=1 wire=1 lead_ms=%lu lookahead_ms=%lu", (unsigned long)LIVE_LEAD_MS, (unsigned long)LOOKAHEAD_MS); break;
      case Operation::Arm: error = arm(); break;
      case Operation::Disarm: stop(true); break;
      case Operation::Stop: stop(); break;
      case Operation::Status:
        snprintf(fields, sizeof(fields), "queue=%u next=%lu player=%s show_ms=%lu file_events=%lu file_error=%s line=%lu audio_tx=%lu clock_tx=%lu event_tx=%lu audio_gap_ms=%lu clock_gap_ms=%lu rejected=%lu capacity_rejects=%lu capacity_profile=%s capacity_tx=%u capacity_window_ms=%lu missed=%lu radio_errors=%lu last_id=%u last_sent=%lu last_due=%lu",
          engine.depth(), (unsigned long)engine.nextTime(), player.stateName(), (unsigned long)player.time(now),
          (unsigned long)player.eventCount(), player.error(), (unsigned long)player.errorLine(),
          (unsigned long)engine.counters.audio, (unsigned long)engine.counters.clock, (unsigned long)engine.counters.events,
          (unsigned long)engine.counters.maxAudioGap, (unsigned long)engine.counters.maxClockGap,
          (unsigned long)engine.counters.rejected, (unsigned long)engine.counters.capacityRejects,
          engine.audioActive() ? "audio" : "no_audio", engine.capacityBudget(), (unsigned long)CAPACITY_WINDOW_MS,
          (unsigned long)engine.counters.missed, (unsigned long)engine.counters.radioErrors,
          engine.counters.lastId, (unsigned long)engine.counters.lastSentMs, (unsigned long)engine.counters.lastExecuteAt); break;
      case Operation::Time: break;
      case Operation::Event:
        if (player.state() == PlayerState::Playing) error = "player_busy";
        else error = live(r.event, id);
        if (!error) snprintf(fields, sizeof(fields), "accepted=1 event_id=%u execute_at=%lu", id, (unsigned long)r.event.executeAt);
        break;
      case Operation::Audio:
        error = hw.audioMode ? hw.audioMode(r.argument) : "unsupported";
        if (!error) engine.setAudioActive(audioActive());
        break;
      case Operation::Load: error = load(r.argument); break;
      case Operation::Play: error = play(); break;
      case Operation::List:
        if (engine.active()) error = "busy";
        else { char names[340]; list(names, sizeof(names)); snprintf(fields, sizeof(fields), "files=%s", names[0] ? names : "none"); }
        break;
      case Operation::PutBegin:
        if (engine.active() || upload || player.state() == PlayerState::Validating) { error = "busy"; break; }
        if (!validFilename(r.argument)) { error = "filename"; break; }
        if (!hw.storage || !hw.storage()) { error = "sd"; break; }
        snprintf(finalPath, sizeof(finalPath), "/shows/%s", r.argument);
        snprintf(uploadPath, sizeof(uploadPath), "/shows/%s.part", r.argument);
        sd->mkdir("/shows");
        if (sd->exists(finalPath)) { error = "exists"; break; }
        sd->remove(uploadPath); upload = sd->open(uploadPath, FILE_WRITE); uploadParser = FileParser{};
        uploadParser.setCapacity(audioActive());
        if (!upload) error = "io";
        break;
      case Operation::PutLine: {
        if (!upload) { error = "no_upload"; break; }
        char copy[LINE_SIZE]; strcpy(copy, r.argument); FileLine parsed;
        error = uploadParser.line(copy, parsed);
        const size_t n = strlen(r.argument);
        if (!error && (upload.size() > 4UL * 1024 * 1024 || upload.write((const uint8_t*)r.argument, n) != n || upload.write('\n') != 1)) error = "io";
        if (error) { upload.close(); sd->remove(uploadPath); }
        break;
      }
      case Operation::PutEnd:
        if (!upload) { error = "no_upload"; break; }
        error = uploadParser.finish(); upload.flush();
        if (!error && upload.getWriteError()) error = "io";
        upload.close();
        if (!error && !sd->rename(uploadPath, finalPath)) error = "io";
        if (error) sd->remove(uploadPath);
        break;
    }
  }
  lastError = error ? error : "none";
  if (error) snprintf(reply, sizeof(reply), "NKSHOW 1 %lu ERROR code=%s state=%s time=%lu", (unsigned long)r.id, error, engine.stateName(), (unsigned long)now);
  else snprintf(reply, sizeof(reply), "NKSHOW 1 %lu OK state=%s time=%lu %s", (unsigned long)r.id, engine.stateName(), (unsigned long)now, fields);
  if (found == 0) cache.remember(r.id, text, reply);
}
void ShowRuntime::tick(Stream* bridge) {
  const uint32_t now = millis();
  NightKiteSync::BeaconInput audio;
  const bool audioEnabled = audioActive(&audio);
  const bool useAudio = engine.active() && audioEnabled;
  engine.setAudioActive(audioEnabled);
  player.tick(engine, now);
  const Transmission tx = engine.next(now, useAudio);
  if (tx.kind != TxKind::None) {
    uint8_t bytes[ADV_SIZE]; size_t n = 0;
    if (tx.kind == TxKind::Audio) { audio.sequence = ++audioSeq; n = NightKiteSync::encodeAdvertising(audio, bytes, sizeof(bytes)).size; }
    else if (encode(tx.packet, bytes, sizeof(bytes))) n = sizeof(bytes);
    const bool ok = n && hw.advertise && hw.advertise(bytes, n);
    engine.sent(tx, millis(), ok);
  }
  if (radioOwned && !engine.active()) { if (hw.off) hw.off(); radioOwned = false; }
  if (!bridge) return;
  if (replyLength) {
    const int available = bridge->availableForWrite();
    if (available > 0) {
      const size_t n = min(size_t(available), replyLength - replyOffset);
      replyOffset += bridge->write((const uint8_t*)reply + replyOffset, n);
      if (replyOffset == replyLength) { replyLength = replyOffset = 0; reply[0] = 0; }
    }
    return;
  }
  for (unsigned budget = 0; budget < 128 && bridge->available(); ++budget) {
    const int c = bridge->read();
    if (c == '\r') continue;
    if (c == '\n') {
      line[length] = 0;
      if (overflow) snprintf(reply, sizeof(reply), "NKSHOW 1 0 ERROR code=line_length");
      else if (length) { bridgeSeen = true; request(line); }
      length = 0; overflow = false;
      if (reply[0]) { strncat(reply, "\n", sizeof(reply) - strlen(reply) - 1); replyLength = strlen(reply); replyOffset = 0; }
      break;
    }
    if (c < 32 || c > 126 || length + 1 >= sizeof(line)) overflow = true;
    if (!overflow) line[length++] = char(c);
  }
}
