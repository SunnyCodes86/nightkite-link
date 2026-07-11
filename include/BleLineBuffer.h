#pragma once

#include <deque>
#include <mutex>
#include <stdint.h>
#include <string>

class BleLineBuffer {
public:
  explicit BleLineBuffer(size_t maxLineLength, size_t maxQueuedLines = 16)
      : maxLineLength(maxLineLength), maxQueuedLines(maxQueuedLines) {}

  bool append(const uint8_t* data, size_t length)
  {
    if (data == nullptr && length != 0) {
      return false;
    }
    std::lock_guard<std::mutex> lock(mutex);
    bool valid = true;
    for (size_t i = 0; i < length; ++i) {
      const char c = static_cast<char>(data[i]);
      if (c == '\r') {
        continue;
      }
      if (discarding) {
        valid = false;
        if (c == '\n') {
          discarding = false;
        }
        continue;
      }
      if (c == '\n') {
        if (!partial.empty()) {
          if (lines.size() < maxQueuedLines) {
            lines.push_back(partial);
          } else {
            valid = false;
          }
          partial.clear();
        }
      } else if (partial.length() < maxLineLength) {
        partial += c;
      } else {
        partial.clear();
        discarding = true;
        valid = false;
      }
    }
    return valid;
  }

  bool pop(std::string& line)
  {
    std::lock_guard<std::mutex> lock(mutex);
    if (lines.empty()) {
      return false;
    }
    line = lines.front();
    lines.pop_front();
    return true;
  }

  void clear()
  {
    std::lock_guard<std::mutex> lock(mutex);
    partial.clear();
    lines.clear();
    discarding = false;
  }

private:
  const size_t maxLineLength;
  const size_t maxQueuedLines;
  std::mutex mutex;
  std::string partial;
  std::deque<std::string> lines;
  bool discarding = false;
};
