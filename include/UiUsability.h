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
  bool saveValid = false;
  bool commandFailure = false;
  uint32_t revision = 0;
  uint32_t savingRevision = 0;

  void patternChanged()
  {
    unsaved = true;
    ++revision;
  }
  void saveStarted()
  {
    savePending = true;
    saveValid = !commandFailure;
    savingRevision = revision;
  }
  bool saveSucceeded()
  {
    const bool savedCurrentRevision = saveValid && savingRevision == revision;
    if (savedCurrentRevision) {
      unsaved = false;
      commandFailure = false;
    }
    savePending = false;
    saveValid = false;
    return savedCurrentRevision;
  }
  void commandFailed()
  {
    unsaved = true;
    commandFailure = true;
    saveValid = false;
  }
  void fullResyncStarted() { commandFailure = false; }
  void saveFailed()
  {
    savePending = false;
    saveValid = false;
  }
};
