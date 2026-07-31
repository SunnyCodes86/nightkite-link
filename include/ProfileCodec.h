#pragma once

#include <cstdint>
#include <string>

struct ProfileData {
  std::string deviceName;
  int brightness = -1;
  int stripLength = -1;
  int activePattern = -1;
  int smoothing = -1;
  int accelRange = -1;
  int gyroRange = -1;
  std::string playMode = "unknown";
  std::string bootMode = "unknown";
  bool syncEnabled = false;
  int syncGroup = -1;
  std::string syncRole = "unknown";
  std::string syncMasterUid;
  std::string syncLossBehavior = "unknown";
  bool wirelessEnabled = false;
  std::string wirelessProfile = "unknown";
  uint32_t enabledPatternMask = 0;
  uint32_t invertedPatternMask = 0;
  bool autoplayEnabled = false;
  int autoplayIntervalSeconds = -1;
};

bool decodeProfileJson(const std::string& json, const ProfileData& fallback, ProfileData& output,
                       std::string& error);
bool encodeProfileJson(const ProfileData& profile, std::string& json, std::string& error);
