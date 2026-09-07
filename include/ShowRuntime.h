#pragma once
#include <Arduino.h>
#include <FS.h>
#include "ShowInput.h"
#include "SyncBeaconCodec.h"

// Target owns hardware; all input paths use this one player/engine/encoder.
class ShowRuntime {
public:
  struct Hardware {
    bool (*arm)();
    void (*off)();
    bool (*advertise)(const uint8_t*, size_t);
    bool (*audio)(NightKiteSync::BeaconInput&);
    const char* (*audioMode)(const char*);
    bool (*storage)();
  };
  void begin(fs::FS& storage, Hardware hardware);
  void tick(Stream* bridge);
  const char* arm();
  void stop(bool disarm = false);
  const char* live(NightKiteShow::Event event, uint16_t& id);
  const char* load(const char* name);
  const char* play();
  void list(char* result, size_t size);
  // For targets that already multiplex a diagnostic console. Same API/cache.
  const char* hostRequest(const char* text);
  NightKiteShow::Engine engine;
  NightKiteShow::Player player;
  bool bridgeSeen = false;
  const char* lastError = "none";
private:
  Hardware hw = {};
  fs::FS* sd = nullptr;
  File file, upload;
  NightKiteShow::FileParser uploadParser;
  NightKiteShow::RequestCache cache;
  char line[NightKiteShow::LINE_SIZE] = {}, reply[512] = {}, uploadPath[64] = {}, finalPath[64] = {};
  size_t length = 0, replyLength = 0, replyOffset = 0;
  bool overflow = false, radioOwned = false;
  uint16_t audioSeq = 0;
  static bool validFilename(const char* name);
  static int readFile(void* context);
  static bool rewindFile(void* context);
  void request(const char* text);
};
