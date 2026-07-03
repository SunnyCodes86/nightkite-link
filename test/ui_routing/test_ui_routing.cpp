#include <assert.h>

#include "UiRouting.h"

int main()
{
  assert(homeView(UiArea::Live) == UiView::LiveHome);
  assert(homeView(UiArea::Setup) == UiView::SetupHub);
  assert(adjacentArea(UiArea::Live, -1) == UiArea::Service);
  assert(adjacentArea(UiArea::Service, 1) == UiArea::Live);

  assert(shortcutView('1') == UiView::Pattern);
  assert(shortcutView('3') == UiView::AudioRun);
  assert(shortcutView('9') == UiView::Firmware);
  assert(shortcutView('H') == UiView::LiveHome);
  assert(!isGlobalViewShortcut('7'));

  assert(isDraftView(UiView::Brightness));
  assert(isDraftView(UiView::AudioTune));
  assert(!isDraftView(UiView::LiveHome));
  assert(draftDisposition(UiView::Brightness, true, false) == DraftDisposition::Apply);
  assert(draftDisposition(UiView::Brightness, false, true) == DraftDisposition::Cancel);
  assert(draftDisposition(UiView::LiveHome, true, false) == DraftDisposition::None);
  assert(isUiLeftKey(','));
  assert(isUiRightKey('/'));
  assert(isUiUpKey(';'));
  assert(isUiDownKey('.'));
  assert(!isUiRightKey('?'));

  assert(getNextPatternId(1, 27) == 2);
  assert(getNextPatternId(2, 27) == 3);
  assert(getNextPatternId(3, 27) == 4);
  assert(getNextPatternId(4, 27) == 5);
  assert(getPrevPatternId(5, 27) == 4);
  assert(getPrevPatternId(4, 27) == 3);
  assert(getPrevPatternId(3, 27) == 2);
  assert(getPrevPatternId(2, 27) == 1);
  assert(getNextPatternId(22, 27) == 23);
  assert(getNextPatternId(26, 27) == 27);
  assert(getNextPatternId(27, 27) == 1);
  assert(getPrevPatternId(1, 27) == 27);

  PatternPersistenceState patternState;
  patternState.markChanged();
  assert(patternState.unsaved);
  patternState.requestSave();
  assert(patternState.unsaved);
  assert(patternState.savePending);
  patternState.saveFailed();
  assert(patternState.unsaved);
  assert(!patternState.savePending);
  patternState.requestSave();
  patternState.confirmSave(23, 0x7ffffff, 0);
  assert(!patternState.unsaved);
  assert(!patternState.savePending);
  assert(patternState.savedPatternId == 23);

  for (int id = 23; id <= 27; ++id) {
    assert(isAudioPattern(id));
  }
  assert(!isAudioPattern(22));
  return 0;
}
