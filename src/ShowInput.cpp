#include "ShowInput.h"
#include <string.h>
#include <stdio.h>

namespace NightKiteShow {
bool number(const char* text, uint32_t maximum, uint32_t& value) {
  if (!text || !*text) return false;
  value = 0;
  for (; *text; ++text) {
    if (*text < '0' || *text > '9' || value > maximum / 10 ||
        (value == maximum / 10 && uint32_t(*text - '0') > maximum % 10)) return false;
    value = value * 10 + *text - '0';
  }
  return true;
}
bool shortId(const char* text, uint32_t& value) {
  if (!text || strlen(text) != 6) return false;
  value = 0;
  for (unsigned i = 0; i < 6; ++i) {
    char c = text[i]; if (c >= 'a' && c <= 'f') c -= 32;
    int n = c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
    if (n < 0) return false;
    value = (value << 4) | n;
  }
  return true;
}
static char* token(char*& text) {
  while (*text == ' ' || *text == '\t') ++text;
  if (!*text) return nullptr;
  char* begin = text;
  while (*text && *text != ' ' && *text != '\t') ++text;
  if (*text) *text++ = 0;
  return begin;
}
static bool sameTarget(const Target& a, const Target& b) { return a.kind == b.kind && a.value == b.value; }
const char* parseEvent(char* text, Event& event) {
  Event e; e.executeAt = event.executeAt;
  char* kind = token(text); uint32_t value;
  if (!kind) return "target";
  if (!strcmp(kind, "ALL")) e.target.kind = TargetKind::All;
  else if (!strcmp(kind, "GROUP")) {
    e.target.kind = TargetKind::Group;
    if (!number(token(text), 255, e.target.value) || !e.target.value) return "target";
  } else if (!strcmp(kind, "SINGLE")) {
    e.target.kind = TargetKind::Single;
    if (!shortId(token(text), e.target.value)) return "target";
  } else return "target";
  char* command = token(text); if (!command) return "command";
  unsigned parameters = 0;
  if (!strcmp(command, "PATTERN")) { e.command = Command::Pattern; parameters = 1; }
  else if (!strcmp(command, "BRIGHTNESS")) { e.command = Command::Brightness; parameters = 1; }
  else if (!strcmp(command, "SOLID")) { e.command = Command::Solid; parameters = 4; }
  else if (!strcmp(command, "BLACKOUT")) e.command = Command::Blackout;
  else if (!strcmp(command, "RELEASE")) e.command = Command::Release;
  else if (!strcmp(command, "CLEAR")) { e.command = Command::Clear; parameters = 2; }
  else if (!strcmp(command, "SEGMENT")) { e.command = Command::Segment; parameters = 7; }
  else if (!strcmp(command, "APPLY")) { e.command = Command::Apply; parameters = 1; }
  else return "command";
  for (unsigned i = 0; i < parameters; ++i) {
    if (!number(token(text), 255, value)) return "parameters";
    e.params[i] = value;
  }
  if (token(text)) return "parameters";
  const char* error = validate(e); if (error) return error;
  event = e; return nullptr;
}
const char* FileParser::line(char* text, FileLine& result) {
  ++lines; result = FileLine{};
  if (char* comment = strchr(text, '#')) *comment = 0;
  char* first = token(text); if (!first) return nullptr;
  if (!header) {
    char* version = token(text);
    if (strcmp(first, "NKSHOW") || !version) return "header";
    if (strcmp(version, "1") || token(text)) return "version";
    header = true; return nullptr;
  }
  if (!strcmp(first, "NAME")) {
    if (events || name[0]) return "name";
    while (*text == ' ' || *text == '\t') ++text;
    size_t n = strlen(text); while (n && (text[n - 1] == ' ' || text[n - 1] == '\t')) --n;
    if (!n || n >= sizeof(name)) return "name";
    memcpy(name, text, n); name[n] = 0; return nullptr;
  }
  Event e;
  if (!number(first, MAX_TIMELINE_MS, e.executeAt)) return "time";
  if (events && e.executeAt < duration) return "time_order";
  const char* error = parseEvent(text, e); if (error) return error;
  if (e.command == Command::Clear) {
    if (imageActive) return "incomplete_image";
    imageActive = true; imageTarget = e.target; imageId = e.params[0]; imageCount = e.params[1]; imageMask = 0;
  } else if (e.command == Command::Segment || e.command == Command::Apply) {
    if (!imageActive || !sameTarget(e.target, imageTarget) || e.params[0] != imageId) return "image";
    if (e.command == Command::Segment) {
      if (e.params[1] >= imageCount || (imageMask & (uint32_t(1) << e.params[1]))) return "segment_index";
      imageMask |= uint32_t(1) << e.params[1];
    } else {
      const uint32_t expected = imageCount == 32 ? UINT32_MAX : (uint32_t(1) << imageCount) - 1;
      if (imageMask != expected) return "incomplete_image";
      imageActive = false;
    }
  } else if (imageActive && e.command == Command::Release) return "incomplete_image";
  if ((error = capacity.admit(e.executeAt))) return error;
  duration = e.executeAt; ++events; result.event = true; result.value = e; return nullptr;
}
const char* FileParser::finish() const { return !header ? "header" : !events ? "empty" : imageActive ? "incomplete_image" : nullptr; }
void Player::load(Reader source, bool audio) {
  reader = source; parser = FileParser{}; length = 0; pending = eof = false;
  parser.setCapacity(audio); capacityAudio = audio; playAfterValidation = false;
  totalEvents = duration = 0; showName[0] = 0; failure = "none";
  mode = reader.read && reader.rewind ? PlayerState::Validating : PlayerState::Error;
}
const char* Player::play(Engine& engine, uint32_t) {
  if (mode != PlayerState::Loaded && mode != PlayerState::End) return "not_loaded";
  if (!engine.ready()) return "not_ready";
  if (engine.depth()) return "queue_busy";
  if (!reader.rewind(reader.context)) return "io";
  parser = FileParser{}; capacityAudio = engine.audioActive(); parser.setCapacity(capacityAudio);
  length = 0; pending = eof = false; playAfterValidation = true; failure = "none";
  mode = PlayerState::Validating; return nullptr;
}
void Player::stop(Engine& engine, uint32_t now) {
  if (mode == PlayerState::Playing || (mode == PlayerState::Validating && playAfterValidation)) mode = PlayerState::Loaded;
  playAfterValidation = false;
  pending = false; engine.stop(now, false);
}
void Player::fail(const char* error, Engine& engine, uint32_t now) {
  if (mode == PlayerState::Playing) engine.stop(now, false);
  failure = error; mode = PlayerState::Error; pending = false;
}
void Player::tick(Engine& engine, uint32_t now) {
  if (mode != PlayerState::Validating && mode != PlayerState::Playing) return;
  if (mode == PlayerState::Validating && playAfterValidation && !engine.ready()) { fail("sender_stopped", engine, now); return; }
  if (mode == PlayerState::Playing && !engine.ready()) { fail("sender_stopped", engine, now); return; }
  if (pending) {
    Event event = nextEvent; event.executeAt += start;
    if (delta(event.executeAt, now) > int32_t(LOOKAHEAD_MS)) return;
    uint16_t id;
    if (const char* error = engine.enqueue(event, now, id)) { fail(error, engine, now); return; }
    pending = false;
  }
  if (eof) {
    if (mode == PlayerState::Playing && delta(now, start + duration) > 250) { mode = PlayerState::End; engine.setPlaying(false); }
    return;
  }
  for (unsigned budget = 0; budget < 512; ++budget) {
    int c = reader.read(reader.context);
    if (c == -2) { fail("io", engine, now); return; }
    if (c == '\r') continue;
    if (c < 0 || c == '\n') {
      buffer[length] = 0; FileLine line;
      const char* error = parser.line(buffer, line); length = 0;
      if (error) { fail(error, engine, now); return; }
      if (c < 0) {
        if ((error = parser.finish())) { fail(error, engine, now); return; }
        eof = true;
        if (mode == PlayerState::Validating) {
          totalEvents = parser.events; duration = parser.duration;
          memcpy(showName, parser.name, sizeof(showName));
          if (!playAfterValidation) { mode = PlayerState::Loaded; return; }
          if (!reader.rewind(reader.context)) { fail("io", engine, now); return; }
          parser = FileParser{}; parser.setCapacity(capacityAudio); length = 0; pending = eof = false;
          playAfterValidation = false; start = now + SHOW_START_LEAD_MS;
          mode = PlayerState::Playing; engine.setPlaying(true); return;
        }
      }
      if (line.event && mode == PlayerState::Playing) { nextEvent = line.value; pending = true; return; }
      if (eof) return;
    } else {
      if (c != '\t' && (c < 32 || c > 126)) { fail("character", engine, now); return; }
      if (length + 1 >= sizeof(buffer)) { fail("line_length", engine, now); return; }
      buffer[length++] = char(c);
    }
  }
}
uint32_t Player::time(uint32_t now) const { return mode == PlayerState::Playing ? (delta(now, start) < 0 ? 0 : now - start) : mode == PlayerState::End ? duration : 0; }
const char* Player::stateName() const {
  switch (mode) {
    case PlayerState::Empty: return "EMPTY"; case PlayerState::Validating: return "VALIDATING";
    case PlayerState::Loaded: return "LOADED"; case PlayerState::Playing: return "PLAYING";
    case PlayerState::End: return "END"; case PlayerState::Error: return "ERROR";
  }
  return "ERROR";
}
const char* parseRequest(const char* input, uint32_t now, Request& r) {
  if (!input || strlen(input) >= LINE_SIZE) return "line_length";
  char buffer[LINE_SIZE]; strcpy(buffer, input); char* text = buffer;
  char* prefix = token(text); char* version = token(text);
  if (!prefix || strcmp(prefix, "NKSHOW") || !version || strcmp(version, "1")) return "version";
  if (!number(token(text), UINT32_MAX, r.id)) return "request_id";
  char* cmd = token(text); if (!cmd) return "command";
  if (!strcmp(cmd, "HELLO") || !strcmp(cmd, "VERSION")) r.operation = Operation::Hello;
  else if (!strcmp(cmd, "ARM")) r.operation = Operation::Arm;
  else if (!strcmp(cmd, "DISARM")) r.operation = Operation::Disarm;
  else if (!strcmp(cmd, "STATUS")) r.operation = Operation::Status;
  else if (!strcmp(cmd, "TIME")) r.operation = Operation::Time;
  else if (!strcmp(cmd, "STOP")) r.operation = Operation::Stop;
  else if (!strcmp(cmd, "PLAY")) r.operation = Operation::Play;
  else if (!strcmp(cmd, "LIST")) r.operation = Operation::List;
  else if (!strcmp(cmd, "AUDIO_STATUS")) r.operation = Operation::AudioStatus;
  else if (!strcmp(cmd, "PUT_END")) r.operation = Operation::PutEnd;
  else if (!strcmp(cmd, "AUDIO") || !strcmp(cmd, "LOAD") || !strcmp(cmd, "PUT_BEGIN")) {
    r.operation = !strcmp(cmd, "AUDIO") ? Operation::Audio : !strcmp(cmd, "LOAD") ? Operation::Load : Operation::PutBegin;
    char* arg = token(text); if (!arg) return "parameters";
    strcpy(r.argument, arg);
  } else if (!strcmp(cmd, "PUT_LINE")) {
    r.operation = Operation::PutLine;
    while (*text == ' ' || *text == '\t') ++text;
    strcpy(r.argument, text); return nullptr;
  } else if (!strcmp(cmd, "EVENT")) {
    r.operation = Operation::Event;
    char* timing = token(text); uint32_t when;
    if (!timing) return "time";
    if (!strcmp(timing, "NOW")) r.event.executeAt = now + LIVE_LEAD_MS;
    else if (!strcmp(timing, "AT")) {
      if (!number(token(text), UINT32_MAX, when)) return "time";
      r.event.executeAt = when;
    } else if (!strcmp(timing, "IN")) {
      if (!number(token(text), MAX_TIMELINE_MS, when)) return "time";
      r.event.executeAt = now + when;
    } else return "time";
    return parseEvent(text, r.event);
  } else return "command";
  return token(text) ? "parameters" : nullptr;
}
int RequestCache::find(uint32_t id, const char* request, const char*& response) const {
  for (uint8_t i = 0; i < count; ++i) if (entries[i].id == id) {
    if (strcmp(request, entries[i].request)) return -1;
    response = entries[i].response; return 1;
  }
  return 0;
}
void RequestCache::remember(uint32_t id, const char* request, const char* response) {
  Entry& e = entries[next]; e.id = id;
  snprintf(e.request, sizeof(e.request), "%s", request); snprintf(e.response, sizeof(e.response), "%s", response);
  next = (next + 1) % 16; if (count < 16) ++count;
}
} // namespace NightKiteShow
