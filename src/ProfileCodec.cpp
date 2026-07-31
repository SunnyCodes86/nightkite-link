#include "ProfileCodec.h"

#include <ArduinoJson.h>
#include <cstring>
#include "NightKitePatterns.h"

namespace {

constexpr int PATTERN_COUNT = 27;
constexpr uint32_t ALL_PATTERN_MASK = (1UL << PATTERN_COUNT) - 1UL;

bool hasMember(JsonObjectConst object, const char* key)
{
  for (JsonPairConst pair : object) {
    if (strcmp(pair.key().c_str(), key) == 0) {
      return true;
    }
  }
  return false;
}

bool fail(std::string& error, const char* key, const char* reason)
{
  error = key;
  error += ' ';
  error += reason;
  return false;
}

bool readInt(JsonObjectConst object, const char* key, int& target, int minValue, int maxValue, std::string& error)
{
  if (!hasMember(object, key)) {
    return true;
  }
  JsonVariantConst value = object[key];
  if (!value.is<int>()) {
    return fail(error, key, "must be integer");
  }
  int parsed = value.as<int>();
  if (parsed < minValue || parsed > maxValue) {
    return fail(error, key, "out of range");
  }
  target = parsed;
  return true;
}

bool readBool(JsonObjectConst object, const char* key, bool& target, std::string& error)
{
  if (!hasMember(object, key)) {
    return true;
  }
  JsonVariantConst value = object[key];
  if (!value.is<bool>()) {
    return fail(error, key, "must be boolean");
  }
  target = value.as<bool>();
  return true;
}

bool readString(JsonObjectConst object, const char* key, std::string& target, std::string& error)
{
  if (!hasMember(object, key)) {
    return true;
  }
  JsonVariantConst value = object[key];
  if (!value.is<const char*>()) {
    return fail(error, key, "must be string");
  }
  target = value.as<const char*>();
  return true;
}

template <size_t N>
bool readChoice(JsonObjectConst object, const char* key, std::string& target, const char* const (&choices)[N],
                std::string& error)
{
  if (!readString(object, key, target, error)) {
    return false;
  }
  if (!hasMember(object, key)) {
    return true;
  }
  for (const char* choice : choices) {
    if (target == choice) {
      return true;
    }
  }
  return fail(error, key, "invalid value");
}

bool readMask(JsonObjectConst object, const char* key, uint32_t& target, bool& present, std::string& error)
{
  present = hasMember(object, key);
  if (!present) {
    return true;
  }
  JsonVariantConst value = object[key];
  if (!value.is<uint32_t>()) {
    return fail(error, key, "must be unsigned integer");
  }
  uint32_t parsed = value.as<uint32_t>();
  if ((parsed & ~ALL_PATTERN_MASK) != 0) {
    return fail(error, key, "contains unsupported patterns");
  }
  target = parsed;
  return true;
}

bool masksFromPatterns(JsonObjectConst settings, bool needEnabled, bool needInverted, uint32_t& enabled,
                       uint32_t& inverted, std::string& error)
{
  if ((!needEnabled && !needInverted) || !hasMember(settings, "patterns")) {
    return true;
  }
  JsonVariantConst value = settings["patterns"];
  if (!value.is<JsonArrayConst>()) {
    return fail(error, "patterns", "must be array");
  }
  uint32_t seen = 0;
  uint32_t parsedEnabled = 0;
  uint32_t parsedInverted = 0;
  for (JsonVariantConst item : value.as<JsonArrayConst>()) {
    if (!item.is<JsonObjectConst>()) {
      return fail(error, "patterns", "entry must be object");
    }
    JsonObjectConst pattern = item.as<JsonObjectConst>();
    if (!pattern["id"].is<int>()) {
      return fail(error, "patterns.id", "must be integer");
    }
    int id = pattern["id"].as<int>();
    if (id < 1 || id > PATTERN_COUNT) {
      return fail(error, "patterns.id", "out of range");
    }
    uint32_t bit = 1UL << (id - 1);
    if (seen & bit) {
      return fail(error, "patterns.id", "duplicate");
    }
    seen |= bit;
    if (hasMember(pattern, "cycle_enabled")) {
      if (!pattern["cycle_enabled"].is<bool>()) {
        return fail(error, "cycle_enabled", "must be boolean");
      }
      if (pattern["cycle_enabled"].as<bool>()) {
        parsedEnabled |= bit;
      }
    }
    if (hasMember(pattern, "inverted")) {
      if (!pattern["inverted"].is<bool>()) {
        return fail(error, "inverted", "must be boolean");
      }
      if (pattern["inverted"].as<bool>()) {
        parsedInverted |= bit;
      }
    }
  }
  if (needEnabled) {
    enabled = parsedEnabled;
  }
  if (needInverted) {
    inverted = parsedInverted;
  }
  return true;
}

}  // namespace

bool encodeProfileJson(const ProfileData& profile, std::string& json, std::string& error)
{
  JsonDocument document;
  document["profile_version"] = 2;
  document["project"] = "NightKite Link";
  document["target"] = "NightKite Multi";
  JsonObject settings = document["settings"].to<JsonObject>();
  if (!profile.deviceName.empty()) settings["device_name"] = profile.deviceName;
  settings["brightness"] = profile.brightness;
  settings["strip_length"] = profile.stripLength;
  settings["active_pattern"] = profile.activePattern;
  settings["smoothing"] = profile.smoothing;
  settings["accel_range"] = profile.accelRange;
  settings["gyro_range"] = profile.gyroRange;
  if (profile.playMode != "unknown") settings["play_mode"] = profile.playMode;
  if (profile.bootMode != "unknown") settings["boot_mode"] = profile.bootMode;
  settings["sync_enabled"] = profile.syncEnabled;
  if (profile.syncGroup >= 1) settings["sync_group"] = profile.syncGroup;
  if (profile.syncRole != "unknown") settings["sync_role"] = profile.syncRole;
  if (!profile.syncMasterUid.empty()) settings["sync_master_uid"] = profile.syncMasterUid;
  if (profile.syncLossBehavior != "unknown") settings["sync_loss_behavior"] = profile.syncLossBehavior;
  settings["wireless_enabled"] = profile.wirelessEnabled;
  if (profile.wirelessProfile != "unknown") settings["wireless_profile"] = profile.wirelessProfile;
  settings["enabled_pattern_mask"] = profile.enabledPatternMask;
  settings["inverted_pattern_mask"] = profile.invertedPatternMask;
  JsonObject autoplay = settings["autoplay"].to<JsonObject>();
  autoplay["enabled"] = profile.autoplayEnabled;
  autoplay["interval_seconds"] = profile.autoplayIntervalSeconds;
  JsonArray patterns = settings["patterns"].to<JsonArray>();
  for (int id = 1; id <= NightKitePatterns::COUNT; ++id) {
    JsonObject item = patterns.add<JsonObject>();
    const uint32_t bit = 1UL << (id - 1);
    item["id"] = id;
    item["name"] = NightKitePatterns::name(id);
    item["cycle_enabled"] = (profile.enabledPatternMask & bit) != 0;
    item["inverted"] = (profile.invertedPatternMask & bit) != 0;
  }
  json.clear();
  serializeJsonPretty(document, json);
  ProfileData decoded;
  ProfileData fallback;
  return !json.empty() && decodeProfileJson(json, fallback, decoded, error);
}

bool decodeProfileJson(const std::string& json, const ProfileData& fallback, ProfileData& output,
                       std::string& error)
{
  JsonDocument document;
  DeserializationError parseError = deserializeJson(document, json);
  if (parseError) {
    error = parseError.c_str();
    return false;
  }
  if (!document.is<JsonObject>()) {
    error = "profile must be object";
    return false;
  }
  JsonObjectConst root = document.as<JsonObjectConst>();
  if (hasMember(root, "profile_version")) {
    if (!root["profile_version"].is<int>()) {
      return fail(error, "profile_version", "must be integer");
    }
    int version = root["profile_version"].as<int>();
    if (version != 1 && version != 2) {
      return fail(error, "profile_version", "unsupported");
    }
  }
  std::string marker;
  if (hasMember(root, "project")) {
    if (!readString(root, "project", marker, error) || marker != "NightKite Link") {
      return fail(error, "project", "unsupported");
    }
  }
  if (hasMember(root, "target")) {
    if (!readString(root, "target", marker, error) || marker != "NightKite Multi") {
      return fail(error, "target", "unsupported");
    }
  }
  if (!root["settings"].is<JsonObjectConst>()) {
    return fail(error, "settings", "must be object");
  }

  output = fallback;
  JsonObjectConst settings = root["settings"].as<JsonObjectConst>();
  const int brightnessLevels[] = {95, 127, 159, 191, 223, 255};
  const int accelRanges[] = {2, 4, 8, 16};
  const int gyroRanges[] = {250, 500, 1000, 2000};
  const char* const playModes[] = {"manual", "autoplay", "sync", "unknown"};
  const char* const bootModes[] = {"last", "manual", "autoplay", "sync", "unknown"};
  const char* const syncRoles[] = {"standalone", "master", "follower", "unknown"};
  const char* const syncLoss[] = {"continue_local", "fallback_autoplay", "warning_only", "unknown"};
  const char* const wirelessProfiles[] = {"long_range", "balanced", "fast_sync", "unknown"};

  if (!readString(settings, "device_name", output.deviceName, error) ||
      !readInt(settings, "strip_length", output.stripLength, 10, 35, error) ||
      !readInt(settings, "active_pattern", output.activePattern, 1, PATTERN_COUNT, error) ||
      !readInt(settings, "smoothing", output.smoothing, 1, 512, error) ||
      !readChoice(settings, "play_mode", output.playMode, playModes, error) ||
      !readChoice(settings, "boot_mode", output.bootMode, bootModes, error) ||
      !readBool(settings, "sync_enabled", output.syncEnabled, error) ||
      !readInt(settings, "sync_group", output.syncGroup, 1, 255, error) ||
      !readChoice(settings, "sync_role", output.syncRole, syncRoles, error) ||
      !readString(settings, "sync_master_uid", output.syncMasterUid, error) ||
      !readChoice(settings, "sync_loss_behavior", output.syncLossBehavior, syncLoss, error) ||
      !readBool(settings, "wireless_enabled", output.wirelessEnabled, error) ||
      !readChoice(settings, "wireless_profile", output.wirelessProfile, wirelessProfiles, error)) {
    return false;
  }

  if (hasMember(settings, "brightness")) {
    if (!settings["brightness"].is<int>()) {
      return fail(error, "brightness", "must be integer");
    }
    int value = settings["brightness"].as<int>();
    bool valid = false;
    for (int allowed : brightnessLevels) valid = valid || value == allowed;
    if (!valid) return fail(error, "brightness", "invalid value");
    output.brightness = value;
  }
  if (hasMember(settings, "accel_range")) {
    if (!settings["accel_range"].is<int>()) return fail(error, "accel_range", "must be integer");
    int value = settings["accel_range"].as<int>();
    bool valid = false;
    for (int allowed : accelRanges) valid = valid || value == allowed;
    if (!valid) return fail(error, "accel_range", "invalid value");
    output.accelRange = value;
  }
  if (hasMember(settings, "gyro_range")) {
    if (!settings["gyro_range"].is<int>()) return fail(error, "gyro_range", "must be integer");
    int value = settings["gyro_range"].as<int>();
    bool valid = false;
    for (int allowed : gyroRanges) valid = valid || value == allowed;
    if (!valid) return fail(error, "gyro_range", "invalid value");
    output.gyroRange = value;
  }

  bool enabledMaskPresent = false;
  bool invertedMaskPresent = false;
  if (!readMask(settings, "enabled_pattern_mask", output.enabledPatternMask, enabledMaskPresent, error) ||
      !readMask(settings, "inverted_pattern_mask", output.invertedPatternMask, invertedMaskPresent, error) ||
      !masksFromPatterns(settings, !enabledMaskPresent, !invertedMaskPresent, output.enabledPatternMask,
                        output.invertedPatternMask, error)) {
    return false;
  }

  JsonObjectConst autoplay;
  if (hasMember(settings, "autoplay")) {
    if (!settings["autoplay"].is<JsonObjectConst>()) {
      return fail(error, "autoplay", "must be object");
    }
    autoplay = settings["autoplay"].as<JsonObjectConst>();
    if (!readBool(autoplay, "enabled", output.autoplayEnabled, error) ||
        !readInt(autoplay, "interval_seconds", output.autoplayIntervalSeconds, 1, 300, error)) {
      return false;
    }
  }
  if (!hasMember(settings, "autoplay") || !hasMember(autoplay, "enabled")) {
    if (!readBool(settings, "autoplay_enabled", output.autoplayEnabled, error)) return false;
  }
  if (!hasMember(settings, "autoplay") || !hasMember(autoplay, "interval_seconds")) {
    if (!readInt(settings, "autoplay_interval", output.autoplayIntervalSeconds, 1, 300, error)) return false;
  }

  error.clear();
  return true;
}
