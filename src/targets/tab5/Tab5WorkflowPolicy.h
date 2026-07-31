#pragma once

#include <cstdint>
#include <cstring>

namespace Tab5WorkflowPolicy {

constexpr int NAV_X = 28;
constexpr int NAV_Y = 108;
constexpr int NAV_WIDTH = 205;
constexpr int NAV_STEP = 58;
constexpr int NAV_BUTTON_HEIGHT = 50;
constexpr int NAV_COUNT = 10;

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

}  // namespace Tab5WorkflowPolicy
