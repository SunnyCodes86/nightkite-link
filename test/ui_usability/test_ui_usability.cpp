#include <assert.h>
#include <string.h>

#include "UiUsability.h"

int main()
{
  assert(fittedFooterHintCount(60, 70, 80, 234, 12) == 3);
  assert(fittedFooterHintCount(300, 20, 20, 234, 12) == 1);
  assert(fittedFooterHintCount(180, 50, 20, 234, 12) == 1);
  assert(fittedFooterHintCount(100, 100, 30, 234, 12) == 2);
  assert(100 + 12 + 100 <= 234);

  assert(strcmp(queueIndicator(0, false), "Q0") == 0);
  assert(strcmp(queueIndicator(1, false), "Q1") == 0);
  assert(strcmp(queueIndicator(9, false), "Q9") == 0);
  assert(strcmp(queueIndicator(10, false), "Q9+") == 0);
  assert(strcmp(queueIndicator(42, true), "Q!") == 0);

  PatternPersistenceState pattern;
  pattern.patternChanged();
  assert(pattern.unsaved);
  // A successful live response changes only the live-command state, not this persistent state.
  assert(pattern.unsaved);
  pattern.saveStarted();
  assert(pattern.unsaved && pattern.savePending);
  pattern.saveFailed();
  assert(pattern.unsaved && !pattern.savePending);

  PatternPersistenceState timedOutSave;
  timedOutSave.patternChanged();
  timedOutSave.saveStarted();
  timedOutSave.saveFailed();
  assert(timedOutSave.unsaved && !timedOutSave.savePending);

  pattern.saveStarted();
  pattern.saveSucceeded();
  assert(!pattern.unsaved && !pattern.savePending);

  PatternPersistenceState queueEmpty;
  queueEmpty.patternChanged();
  assert(queueEmpty.unsaved);

  PatternPersistenceState brightnessUnaffected;
  assert(!brightnessUnaffected.unsaved);
  PatternPersistenceState navigation;
  navigation.patternChanged();
  PatternPersistenceState afterNavigation = navigation;
  assert(afterNavigation.unsaved);
  return 0;
}
