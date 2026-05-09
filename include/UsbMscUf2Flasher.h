#pragma once

#include <Arduino.h>
#include <FS.h>
#include <cstdio>
#include <stddef.h>

enum class FlashResult {
  Ok,
  SdNotReady,
  FileMissing,
  InvalidUf2,
  UsbHostInitFailed,
  NoMassStorageDevice,
  MountFailed,
  OpenSourceFailed,
  OpenTargetFailed,
  WriteFailed,
  DeviceDisconnectedTooEarly,
  Timeout,
  Cancelled,
  UnknownError,
};

struct FlashProgress {
  size_t totalBytes = 0;
  size_t copiedBytes = 0;
  int percent = 0;
  String message;
  FlashResult result = FlashResult::UnknownError;
  bool done = false;
  bool success = false;
  bool massStorageConnected = false;
};

class UsbMscUf2Flasher {
public:
  bool begin();
  void end();

  bool isRunning() const;
  bool isMassStorageConnected() const;

  bool startFlash(const String& sdUf2Path, const String& displayName);
  void poll();
  void cancel();

  const FlashProgress& progress() const;
  FlashResult result() const;
  const char* resultMessage() const;

private:
  enum class State : uint8_t {
    Idle,
    Installing,
    WaitingForDevice,
    Mounting,
    Copying,
    WaitingForReboot,
    Done,
    Error,
  };

  static void mscEventCallback(const void* event, void* arg);

  void setError(FlashResult result, const String& message);
  void cleanup();
  bool installMscHost();
  bool mountDevice();
  bool copyChunk();
  void updatePercent();

  State state = State::Idle;
  FlashProgress current;
  String sourcePath;
  String displayName;
  unsigned long stateStartedMs = 0;
  unsigned long lastProgressLogMs = 0;
  bool mscInstalled = false;
  bool deviceInstalled = false;
  bool vfsMounted = false;
  volatile bool connectedEvent = false;
  volatile bool disconnectedEvent = false;
  uint8_t deviceAddress = 0;
  void* deviceHandle = nullptr;
  void* vfsHandle = nullptr;
  File sourceFile;
  FILE* targetFile = nullptr;
};
