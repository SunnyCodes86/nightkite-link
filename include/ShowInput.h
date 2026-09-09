#pragma once
#include "ShowControl.h"

namespace NightKiteShow {
bool number(const char* text, uint32_t maximum, uint32_t& value);
bool shortId(const char* text, uint32_t& value);
// Shared command grammar: TARGET COMMAND parameters; input is modified in place.
const char* parseEvent(char* text, Event& event);
struct FileLine { bool event = false; Event value; };
class FileParser {
public:
  void setCapacity(bool audio) { capacity.reset(audio, SHOW_START_LEAD_MS); }
  const char* line(char* text, FileLine& result);
  const char* finish() const;
  uint32_t events = 0, duration = 0, lines = 0;
  char name[41] = {};
private:
  bool header = false, imageActive = false;
  Target imageTarget;
  uint8_t imageId = 0, imageCount = 0;
  uint32_t imageMask = 0;
  TimelineCapacity capacity;
};
struct Reader {
  void* context = nullptr;
  int (*read)(void*) = nullptr; // -1 EOF, -2 I/O error.
  bool (*rewind)(void*) = nullptr;
};
enum class PlayerState : uint8_t { Empty, Validating, Loaded, Playing, End, Error };
class Player {
public:
  void load(Reader source, bool audio = false);
  const char* play(Engine& engine, uint32_t now);
  void stop(Engine& engine, uint32_t now);
  void tick(Engine& engine, uint32_t now);
  PlayerState state() const { return mode; }
  const char* stateName() const;
  const char* error() const { return failure; }
  const char* name() const { return showName; }
  uint32_t time(uint32_t now) const;
  uint32_t eventCount() const { return totalEvents; }
  uint32_t errorLine() const { return parser.lines; }
private:
  Reader reader;
  FileParser parser;
  PlayerState mode = PlayerState::Empty;
  char buffer[LINE_SIZE] = {}, showName[41] = {};
  size_t length = 0;
  bool pending = false, eof = false, playAfterValidation = false, capacityAudio = false;
  Event nextEvent;
  uint32_t start = 0, totalEvents = 0, duration = 0;
  const char* failure = "none";
  void fail(const char* error, Engine& engine, uint32_t now);
};

enum class Operation : uint8_t { Hello, Arm, Disarm, Status, Time, Event, Stop, Audio, AudioStatus, Load, Play, List, PutBegin, PutLine, PutEnd };
struct Request {
  uint32_t id = 0;
  Operation operation = Operation::Hello;
  Event event;
  char argument[LINE_SIZE] = {};
};
const char* parseRequest(const char* line, uint32_t now, Request& request);
class RequestCache {
public:
  // 0 new, 1 cached reply, -1 same ID with different request.
  int find(uint32_t id, const char* request, const char*& response) const;
  void remember(uint32_t id, const char* request, const char* response);
private:
  struct Entry { uint32_t id = 0; char request[LINE_SIZE] = {}, response[512] = {}; };
  Entry entries[16];
  uint8_t next = 0, count = 0;
};
} // namespace NightKiteShow
