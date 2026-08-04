#include <assert.h>

#include "Tab5WorkflowPolicy.h"

int main()
{
  using namespace Tab5WorkflowPolicy;
  assert(navIndexAt(28, 108) == 0);
  assert(navIndexAt(232, 157) == 0);
  assert(navIndexAt(28, 158) == -1);
  assert(navIndexAt(100, 108 + 9 * 58) == 9);
  assert(navIndexAt(244, 108) == -1);
  assert(supportedPatternMask(22) == 0x003FFFFFUL);
  assert(supportedPatternMask(27) == 0x07FFFFFFUL);
  assert(terminalCommandAllowed("cmd=status"));
  assert(terminalCommandAllowed("cmd=get section=sync"));
  assert(!terminalCommandAllowed("cmd=save"));
  assert(!terminalCommandAllowed("cmd=defaults confirm=1"));
  assert(!terminalCommandAllowed("status"));
  assert(!terminalCommandAllowed("cmd=status\ncmd=save"));
  assert(strcmp(nextBootCalibration("quick"), "off") == 0);
  assert(strcmp(nextBootCalibration("off"), "quick") == 0);
  assert(tapTempoBpm(1000, 1500, 120) == 120);
  assert(tapTempoBpm(1000, 1400, 120) == 150);
  assert(tapTempoBpm(0, 1000, 90) == 90);
  assert(tapTempoBpm(1000, 2800, 90) == 40);
  assert(tapTempoBpm(1000, 3001, 90) == 90);
  assert(!pollDue(4999, 1000, 4000));
  assert(pollDue(5000, 1000, 4000));
  assert(pollDue(10, UINT32_MAX - 9, 20));

  assert(nativeToLogicalPoint(0, 1279).x == 0 && nativeToLogicalPoint(0, 1279).y == 0);
  assert(nativeToLogicalPoint(719, 0).x == 1279 && nativeToLogicalPoint(719, 0).y == 719);
  assert(logicalToNativePoint(0, 0).x == 0 && logicalToNativePoint(0, 0).y == 1279);
  assert(logicalToNativePoint(1279, 719).x == 719 && logicalToNativePoint(1279, 719).y == 0);
  assert(logicalToNativePoint(640, 360).x == 360 && logicalToNativePoint(640, 360).y == 639);
  const UiRegion nativeFull = logicalToNativeRegion({0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT});
  assert(nativeFull.x == 0 && nativeFull.y == 0 && nativeFull.width == 720 && nativeFull.height == 1280);
  const UiRegion nativeModal = logicalToNativeRegion({120, 90, 1040, 570});
  assert(nativeModal.x == 90 && nativeModal.y == 120 && nativeModal.width == 570 && nativeModal.height == 1040);
  const UiRegion nativeKeyboard = logicalToNativeRegion({175, 255, 780, 62});
  assert(nativeKeyboard.x == 255 && nativeKeyboard.y == 325 && nativeKeyboard.width == 62 && nativeKeyboard.height == 780);

  const UiRegion panelInterior = {273, 114, 971, 574};
  assert(regionContains(panelInterior, {275, 165, 955, 102}));
  assert(regionContains(panelInterior, panelInterior));
  assert(!regionContains(panelInterior, {255, 96, 1007, 610}));
  assert(!regionContains(panelInterior, {1240, 680, 10, 10}));

  UiRegion regions[] = {{10, 10, 100, 40}, {105, 30, 80, 40}, {400, 10, 50, 50}};
  size_t regionCount = coalesceRegions(regions, 3);
  assert(regionCount == 2);
  assert(regionsIntersect(regions[0], 180, 60, 10, 10));
  assert(!regionsIntersect(regions[0], 300, 300, 10, 10));

  UiRegion large[] = {{0, 0, 640, 720}, {640, 0, 1, 720}};
  regionCount = coalesceRegions(large, 2);
  assert(regionCount == 1);
  assert(large[0].width == DISPLAY_WIDTH && large[0].height == DISPLAY_HEIGHT);

  UiRegion bounded[] = {{0, 0, 20, 20}, {200, 0, 20, 20}};
  regionCount = addRegion(bounded, 2, 2, {25, 0, 20, 20});
  assert(regionCount == 2);
  assert(bounded[0].width == 45);
  return 0;
}
