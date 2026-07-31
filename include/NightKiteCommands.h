#pragma once

#include <Arduino.h>

class NightKiteCommands {
public:
  static String refreshAll() { return "show"; }
  static String refreshPatterns() { return "patterns"; }
  static String refreshBattery() { return "battery"; }
  static String setBrightness(int value) { return "set brightness " + String(value); }
  static String setStripLength(int value) { return "set strip_length " + String(value); }
  static String setPattern(int value) { return "set pattern " + String(value); }
  static String setSmoothing(int value) { return "set smoothing " + String(value); }
  static String setAccelRange(int value) { return "set accel_range " + String(value); }
  static String setGyroRange(int value) { return "set gyro_range " + String(value); }
  static String setAutoplay(bool enabled) { return String("set autoplay ") + (enabled ? "on" : "off"); }
  static String setAutoplayInterval(int seconds) { return "set autoplay_interval " + String(seconds); }
  static String setPatternCycle(int id, bool enabled)
  {
    return String(enabled ? "enable_pattern " : "disable_pattern ") + String(id);
  }
  static String setPatternInvert(int id, bool inverted)
  {
    return String(inverted ? "invert_pattern " : "normal_pattern ") + String(id);
  }
  static String setAllCycle(bool enabled, const String& patternList)
  {
    return String(enabled ? "enable_pattern " : "disable_pattern ") + patternList;
  }
  static String setAllInvert(bool inverted, const String& patternList)
  {
    return String(inverted ? "invert_pattern " : "normal_pattern ") + patternList;
  }
};
