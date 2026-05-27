#pragma once

struct ControllerBatteryParseResult {
  bool hasVoltage;
  float voltage;
  bool hasPercent;
  int percent;
  bool hasState;
  char state[24];
};

void resetControllerBatteryParseResult(ControllerBatteryParseResult* result);
bool parseControllerBatteryLine(const char* line, ControllerBatteryParseResult* result);
const char* compactControllerBatteryStateLabel(const char* state);
