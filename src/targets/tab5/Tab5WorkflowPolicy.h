#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace Tab5WorkflowPolicy {

constexpr int NAV_X = 28;
constexpr int NAV_Y = 108;
constexpr int NAV_WIDTH = 205;
constexpr int NAV_STEP = 58;
constexpr int NAV_BUTTON_HEIGHT = 50;
constexpr int NAV_COUNT = 10;
constexpr int DISPLAY_WIDTH = 1280;
constexpr int DISPLAY_HEIGHT = 720;
constexpr int DIRTY_MERGE_GAP = 16;
constexpr uint32_t FULL_REFRESH_PERCENT = 45;

struct UiRegion {
  int16_t x;
  int16_t y;
  int16_t width;
  int16_t height;
};

struct UiPoint {
  int16_t x;
  int16_t y;
};

inline UiPoint nativeToLogicalPoint(int x, int y)
{
  return {static_cast<int16_t>(DISPLAY_WIDTH - 1 - y), static_cast<int16_t>(x)};
}

inline UiPoint logicalToNativePoint(int x, int y)
{
  return {static_cast<int16_t>(y), static_cast<int16_t>(DISPLAY_WIDTH - 1 - x)};
}

inline UiRegion logicalToNativeRegion(const UiRegion& region)
{
  return {region.y, static_cast<int16_t>(DISPLAY_WIDTH - region.x - region.width),
          region.height, region.width};
}

inline uint32_t regionArea(const UiRegion& region)
{
  return static_cast<uint32_t>(region.width) * region.height;
}

inline bool regionsIntersect(const UiRegion& region, int x, int y, int width, int height)
{
  return region.x < x + width && x < region.x + region.width &&
         region.y < y + height && y < region.y + region.height;
}

inline bool regionContains(const UiRegion& outer, const UiRegion& inner)
{
  return inner.x >= outer.x && inner.y >= outer.y &&
         inner.x + inner.width <= outer.x + outer.width &&
         inner.y + inner.height <= outer.y + outer.height;
}

inline UiRegion unionRegion(const UiRegion& a, const UiRegion& b)
{
  const int left = a.x < b.x ? a.x : b.x;
  const int top = a.y < b.y ? a.y : b.y;
  const int right = a.x + a.width > b.x + b.width ? a.x + a.width : b.x + b.width;
  const int bottom = a.y + a.height > b.y + b.height ? a.y + a.height : b.y + b.height;
  return {static_cast<int16_t>(left), static_cast<int16_t>(top),
          static_cast<int16_t>(right - left), static_cast<int16_t>(bottom - top)};
}

inline size_t coalesceRegions(UiRegion* regions, size_t count)
{
  bool merged;
  do {
    merged = false;
    for (size_t i = 0; i < count && !merged; ++i) {
      for (size_t j = i + 1; j < count; ++j) {
        const int gapX = regions[i].x > regions[j].x + regions[j].width
                             ? regions[i].x - (regions[j].x + regions[j].width)
                         : regions[j].x > regions[i].x + regions[i].width
                             ? regions[j].x - (regions[i].x + regions[i].width)
                             : 0;
        const int gapY = regions[i].y > regions[j].y + regions[j].height
                             ? regions[i].y - (regions[j].y + regions[j].height)
                         : regions[j].y > regions[i].y + regions[i].height
                             ? regions[j].y - (regions[i].y + regions[i].height)
                             : 0;
        if (gapX > DIRTY_MERGE_GAP || gapY > DIRTY_MERGE_GAP) continue;
        const UiRegion combined = unionRegion(regions[i], regions[j]);
        regions[i] = combined;
        regions[j] = regions[--count];
        merged = true;
        break;
      }
    }
  } while (merged);

  uint32_t area = 0;
  for (size_t i = 0; i < count; ++i) area += regionArea(regions[i]);
  if (area * 100 >= static_cast<uint32_t>(DISPLAY_WIDTH) * DISPLAY_HEIGHT * FULL_REFRESH_PERCENT) {
    regions[0] = {0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT};
    return 1;
  }
  return count;
}

inline size_t addRegion(UiRegion* regions, size_t count, size_t capacity, const UiRegion& candidate)
{
  count = coalesceRegions(regions, count);
  if (count == 1 && regions[0].x == 0 && regions[0].y == 0 &&
      regions[0].width == DISPLAY_WIDTH && regions[0].height == DISPLAY_HEIGHT) return count;
  if (count < capacity) {
    regions[count++] = candidate;
    return coalesceRegions(regions, count);
  }

  size_t best = 0;
  uint32_t bestAddedArea = UINT32_MAX;
  for (size_t i = 0; i < count; ++i) {
    const uint32_t addedArea = regionArea(unionRegion(regions[i], candidate)) - regionArea(regions[i]);
    if (addedArea < bestAddedArea) {
      best = i;
      bestAddedArea = addedArea;
    }
  }
  regions[best] = unionRegion(regions[best], candidate);
  return coalesceRegions(regions, count);
}

inline int navIndexAt(int x, int y)
{
  if (x < NAV_X || x >= NAV_X + NAV_WIDTH || y < NAV_Y) return -1;
  const int offset = y - NAV_Y;
  const int index = offset / NAV_STEP;
  return index < NAV_COUNT && offset % NAV_STEP < NAV_BUTTON_HEIGHT ? index : -1;
}

inline uint32_t supportedPatternMask(int count)
{
  return count <= 0 ? 0 : count >= 27 ? 0x07FFFFFFUL : (1UL << count) - 1UL;
}

inline bool terminalCommandAllowed(const char* command)
{
  if (command == nullptr || std::strncmp(command, "cmd=", 4) != 0 || std::strchr(command, '\n') != nullptr ||
      std::strchr(command, '\r') != nullptr) return false;
  return std::strncmp(command, "cmd=defaults", 12) != 0 && std::strncmp(command, "cmd=save", 8) != 0;
}

inline const char* nextBootCalibration(const char* current)
{
  return current != nullptr && std::strcmp(current, "quick") == 0 ? "off" : "quick";
}

inline int tapTempoBpm(uint32_t previousTapAt, uint32_t now, int fallback)
{
  const uint32_t interval = now - previousTapAt;
  if (previousTapAt == 0 || interval < 250 || interval > 2000) return fallback;
  const int bpm = static_cast<int>(60000UL / interval);
  return bpm < 40 ? 40 : bpm > 240 ? 240 : bpm;
}

inline bool pollDue(uint32_t now, uint32_t last, uint32_t interval)
{
  return now - last >= interval;
}

}  // namespace Tab5WorkflowPolicy
