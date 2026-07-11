#pragma once

#include <limits.h>
#include <stdint.h>

enum class InitialRefreshPart : uint8_t {
  Info = 1 << 0,
  Status = 1 << 1,
  Config = 1 << 2,
  Play = 1 << 3,
};

struct InitialRefreshState {
  uint8_t completed = 0;

  void reset() { completed = 0; }

  void commandSucceeded(InitialRefreshPart part) { completed |= static_cast<uint8_t>(part); }

  void legacyShowSucceeded() { completed = requiredMask(); }

  bool ready() const { return completed == requiredMask(); }

private:
  static constexpr uint8_t requiredMask()
  {
    return static_cast<uint8_t>(InitialRefreshPart::Info) | static_cast<uint8_t>(InitialRefreshPart::Status) |
           static_cast<uint8_t>(InitialRefreshPart::Config) | static_cast<uint8_t>(InitialRefreshPart::Play);
  }
};

inline bool controllerSessionReady(bool transportConnected, bool controllerConnected, bool initialRefreshReady,
                                   bool protocolResolved)
{
  return transportConnected && controllerConnected && initialRefreshReady && protocolResolved;
}

inline int currentBeaconAgeMs(int reportedAgeMs, uint32_t reportedAtMs, uint32_t nowMs)
{
  if (reportedAgeMs < 0) {
    return -1;
  }
  const uint32_t elapsedMs = nowMs - reportedAtMs;
  if (elapsedMs > static_cast<uint32_t>(INT_MAX - reportedAgeMs)) {
    return INT_MAX;
  }
  return reportedAgeMs + static_cast<int>(elapsedMs);
}
