#pragma once

#include <cstdlib>
#include <string>

struct Nk4Line {
  bool valid = false;
  bool event = false;
  bool ok = false;
  bool error = false;
  int sequence = -1;
  std::string text;

  std::string value(const char* key) const
  {
    const std::string token = std::string(key) + "=";
    size_t start = 0;
    while ((start = text.find(token, start)) != std::string::npos) {
      if (start == 0 || text[start - 1] == ' ') {
        start += token.size();
        const size_t end = text.find(' ', start);
        return text.substr(start, end == std::string::npos ? end : end - start);
      }
      ++start;
    }
    return {};
  }
};

inline Nk4Line parseNk4Line(const std::string& text)
{
  Nk4Line line;
  line.text = text;
  line.valid = text.compare(0, 4, "NK4 ") == 0;
  if (!line.valid) {
    return line;
  }
  line.event = text.find(" event=") != std::string::npos;
  line.ok = text.find(" ok") != std::string::npos;
  line.error = text.find(" err") != std::string::npos;
  const std::string sequence = line.value("seq");
  if (!sequence.empty()) {
    line.sequence = static_cast<int>(std::strtol(sequence.c_str(), nullptr, 10));
  }
  return line;
}
