#pragma once

#include <cstdint>

enum class UiArea : uint8_t {
  Live,
  Show,
  Setup,
  Service,
};

enum class UiView : uint8_t {
  LiveHome,
  Pattern,
  Brightness,
  Play,
  AudioRun,
  AudioTune,
  Profiles,
  SetupHub,
  StripSmooth,
  Motion,
  Wireless,
  DeviceName,
  SaveDefaults,
  SyncSetup,
  SyncDiagnostics,
  Connect,
  ServiceHub,
  StatusDump,
  Firmware,
  Confirm,
  Help,
};

enum class DraftDisposition : uint8_t {
  None,
  Apply,
  Cancel,
};

struct PatternPersistenceState {
  bool unsaved = false;
  bool savePending = false;
  int savedPatternId = -1;
  uint32_t savedEnabledMask = 0;
  uint32_t savedInvertedMask = 0;

  void markChanged() { unsaved = true; }
  void requestSave() { savePending = true; }
  void saveFailed() { savePending = false; }
  void confirmSave(int patternId, uint32_t enabledMask, uint32_t invertedMask)
  {
    savedPatternId = patternId;
    savedEnabledMask = enabledMask;
    savedInvertedMask = invertedMask;
    unsaved = false;
    savePending = false;
  }
};

constexpr UiView homeView(UiArea area)
{
  switch (area) {
    case UiArea::Live: return UiView::LiveHome;
    case UiArea::Show: return UiView::Pattern;
    case UiArea::Setup: return UiView::SetupHub;
    case UiArea::Service: return UiView::ServiceHub;
  }
  return UiView::LiveHome;
}

constexpr UiArea areaForView(UiView view)
{
  switch (view) {
    case UiView::Pattern:
    case UiView::Brightness:
    case UiView::Play:
    case UiView::AudioRun:
    case UiView::AudioTune:
    case UiView::Profiles:
    case UiView::SyncSetup:
      return UiArea::Show;
    case UiView::SetupHub:
    case UiView::StripSmooth:
    case UiView::Motion:
    case UiView::Wireless:
    case UiView::DeviceName:
    case UiView::SaveDefaults:
      return UiArea::Setup;
    case UiView::SyncDiagnostics:
    case UiView::Connect:
    case UiView::ServiceHub:
    case UiView::StatusDump:
    case UiView::Firmware:
      return UiArea::Service;
    case UiView::LiveHome:
    case UiView::Confirm:
    case UiView::Help:
    default:
      return UiArea::Live;
  }
}

constexpr UiArea adjacentArea(UiArea area, int delta)
{
  int next = (static_cast<int>(area) + (delta < 0 ? 3 : 1)) % 4;
  return static_cast<UiArea>(next);
}

constexpr UiView shortcutView(char key)
{
  switch (key) {
    case '1': return UiView::Pattern;
    case '2': return UiView::Brightness;
    case '3': return UiView::AudioRun;
    case '4': return UiView::Profiles;
    case '5': return UiView::SyncSetup;
    case '6': return UiView::Connect;
    case '8': return UiView::SyncDiagnostics;
    case '9': return UiView::Firmware;
    case 'h':
    case 'H': return UiView::LiveHome;
    default: return UiView::Confirm;
  }
}

constexpr bool isGlobalViewShortcut(char key)
{
  return shortcutView(key) != UiView::Confirm;
}

constexpr bool isAudioPattern(int patternId)
{
  return patternId >= 23 && patternId <= 27;
}

constexpr bool isDraftView(UiView view)
{
  return view == UiView::Pattern || view == UiView::Brightness || view == UiView::Play ||
         view == UiView::AudioTune || view == UiView::StripSmooth || view == UiView::Wireless;
}

constexpr DraftDisposition draftDisposition(UiView view, bool enter, bool back)
{
  return !isDraftView(view) ? DraftDisposition::None
       : enter ? DraftDisposition::Apply
       : back ? DraftDisposition::Cancel
              : DraftDisposition::None;
}

constexpr bool isUiLeftKey(char key)
{
  return key == 'a' || key == 'A' || key == ',' || key == '<';
}

constexpr bool isUiRightKey(char key)
{
  return key == 'd' || key == 'D' || key == '/';
}

constexpr bool isUiUpKey(char key)
{
  return key == 'w' || key == 'W' || key == ';' || key == ':';
}

constexpr bool isUiDownKey(char key)
{
  return key == 's' || key == 'S' || key == '.' || key == '>';
}

constexpr int getNextPatternId(int patternId, int patternCount)
{
  return patternId >= patternCount || patternId < 1 ? 1 : patternId + 1;
}

constexpr int getPrevPatternId(int patternId, int patternCount)
{
  return patternId <= 1 || patternId > patternCount ? patternCount : patternId - 1;
}
