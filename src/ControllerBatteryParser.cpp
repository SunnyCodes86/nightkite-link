#include "ControllerBatteryParser.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

namespace {

bool isBatteryState(const char* value)
{
  return strcmp(value, "NORMAL") == 0 || strcmp(value, "LOW_WARNING") == 0 || strcmp(value, "CRITICAL") == 0 ||
         strcmp(value, "SOFT_CUTOFF") == 0 || strcmp(value, "EMERGENCY_CUTOFF") == 0;
}

bool valueForKey(const char* line, const char* key, char* output, size_t outputSize)
{
  if (line == nullptr || key == nullptr || output == nullptr || outputSize == 0) {
    return false;
  }
  output[0] = '\0';
  const size_t keyLen = strlen(key);
  const char* cursor = line;
  while ((cursor = strstr(cursor, key)) != nullptr) {
    const bool keyStart = cursor == line || isspace(static_cast<unsigned char>(cursor[-1]));
    if (keyStart && cursor[keyLen] == '=') {
      const char* valueStart = cursor + keyLen + 1;
      size_t len = 0;
      while (valueStart[len] != '\0' && !isspace(static_cast<unsigned char>(valueStart[len])) && len + 1 < outputSize) {
        ++len;
      }
      memcpy(output, valueStart, len);
      output[len] = '\0';
      return len > 0;
    }
    cursor += keyLen;
  }
  return false;
}

bool parseFloatKey(const char* line, const char* key, float* output)
{
  char value[24];
  if (!valueForKey(line, key, value, sizeof(value))) {
    return false;
  }
  char* end = nullptr;
  float parsed = strtof(value, &end);
  if (end == value) {
    return false;
  }
  *output = parsed;
  return true;
}

bool parseIntKey(const char* line, const char* key, int* output)
{
  char value[16];
  if (!valueForKey(line, key, value, sizeof(value))) {
    return false;
  }
  char* end = nullptr;
  long parsed = strtol(value, &end, 10);
  if (end == value || parsed < 0 || parsed > 100) {
    return false;
  }
  *output = static_cast<int>(parsed);
  return true;
}

} // namespace

void resetControllerBatteryParseResult(ControllerBatteryParseResult* result)
{
  if (result == nullptr) {
    return;
  }
  result->hasVoltage = false;
  result->voltage = 0.0f;
  result->hasPercent = false;
  result->percent = -1;
  result->hasState = false;
  result->state[0] = '\0';
}

bool parseControllerBatteryLine(const char* line, ControllerBatteryParseResult* result)
{
  if (result == nullptr) {
    return false;
  }
  resetControllerBatteryParseResult(result);
  if (line == nullptr) {
    return false;
  }

  float voltage = 0.0f;
  if (parseFloatKey(line, "battery_voltage", &voltage) || parseFloatKey(line, "battery_v", &voltage) ||
      parseFloatKey(line, "controller_battery_voltage", &voltage)) {
    if (voltage > 0.5f && voltage < 6.5f) {
      result->voltage = voltage;
      result->hasVoltage = true;
    }
  } else {
    float batteryMv = 0.0f;
    if (parseFloatKey(line, "battery_mv", &batteryMv) && batteryMv > 500.0f && batteryMv < 6500.0f) {
      result->voltage = batteryMv / 1000.0f;
      result->hasVoltage = true;
    }
  }

  int percent = -1;
  if (parseIntKey(line, "battery_percent", &percent) || parseIntKey(line, "battery_pct", &percent) ||
      parseIntKey(line, "controller_battery_percent", &percent)) {
    result->percent = percent;
    result->hasPercent = true;
  }

  char state[24];
  if (valueForKey(line, "battery_state", state, sizeof(state)) && isBatteryState(state)) {
    strncpy(result->state, state, sizeof(result->state) - 1);
    result->state[sizeof(result->state) - 1] = '\0';
    result->hasState = true;
  }

  return result->hasVoltage || result->hasPercent || result->hasState;
}

const char* compactControllerBatteryStateLabel(const char* state)
{
  if (state == nullptr) {
    return "";
  }
  if (strcmp(state, "LOW_WARNING") == 0) return "LOW";
  if (strcmp(state, "CRITICAL") == 0) return "CRIT";
  if (strcmp(state, "SOFT_CUTOFF") == 0) return "CUT";
  if (strcmp(state, "EMERGENCY_CUTOFF") == 0) return "EMPTY";
  return "";
}
