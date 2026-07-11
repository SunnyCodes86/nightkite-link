#pragma once

#include <cstddef>
#include <cstdint>

inline uint8_t fittedFooterHintCount(int primaryWidth, int secondaryWidth, int tertiaryWidth,
                                     int maxWidth, int spacingWidth)
{
  int used = primaryWidth;
  uint8_t count = 1;
  if (secondaryWidth > 0 && used + spacingWidth + secondaryWidth <= maxWidth) {
    used += spacingWidth + secondaryWidth;
    count = 2;
  }
  if (count == 2 && tertiaryWidth > 0 && used + spacingWidth + tertiaryWidth <= maxWidth) {
    count = 3;
  }
  return count;
}

inline const char* queueIndicator(std::size_t pending, bool error)
{
  static const char* const TOKENS[] = {"Q0", "Q1", "Q2", "Q3", "Q4", "Q5", "Q6", "Q7", "Q8", "Q9"};
  return error ? "Q!" : pending > 9 ? "Q9+" : TOKENS[pending];
}

struct PatternPersistenceState {
  bool unsaved = false;
  bool savePending = false;

  void patternChanged() { unsaved = true; }
  void saveStarted() { savePending = true; }
  void saveSucceeded()
  {
    unsaved = false;
    savePending = false;
  }
  void saveFailed() { savePending = false; }
};
