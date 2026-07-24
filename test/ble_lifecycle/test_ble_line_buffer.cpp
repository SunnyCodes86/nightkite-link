#include <assert.h>
#include <stdint.h>

#include <algorithm>
#include <string>
#include <thread>

#include "BleLineBuffer.h"

static void append(BleLineBuffer& buffer, const char* text)
{
  assert(buffer.append(reinterpret_cast<const uint8_t*>(text), std::char_traits<char>::length(text)));
}

static void testLongNk4ResponseAndSequenceError()
{
  BleLineBuffer buffer(4096);
  const std::string prefix = "NK4 seq=42 ok payload=";
  const std::string response = prefix + std::string(4094 - prefix.size(), 'x') + "\n";
  for (size_t offset = 0; offset < response.size(); offset += 20) {
    const size_t chunk = std::min<size_t>(20, response.size() - offset);
    assert(buffer.append(reinterpret_cast<const uint8_t*>(response.data() + offset), chunk));
  }

  std::string line;
  assert(buffer.pop(line) && line == response.substr(0, response.size() - 1));
  append(buffer, "NK4 seq=77 err code=range_error msg=line_too_long\n");
  assert(buffer.pop(line) && line == "NK4 seq=77 err code=range_error msg=line_too_long");
}

int main()
{
  testLongNk4ResponseAndSequenceError();
  BleLineBuffer buffer(16);
  append(buffer, "NK4 seq=");
  append(buffer, "1\r\nnext\n");
  std::string line;
  assert(buffer.pop(line) && line == "NK4 seq=1");
  assert(buffer.pop(line) && line == "next");
  assert(!buffer.pop(line));

  const uint8_t overflow[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h'};
  assert(!buffer.append(overflow, sizeof(overflow)));
  assert(!buffer.append(reinterpret_cast<const uint8_t*>("bad\n"), 4));
  append(buffer, "ok\n");
  assert(buffer.pop(line) && line == "ok");

  std::thread producer([&buffer]() { append(buffer, "async\n"); });
  producer.join();
  assert(buffer.pop(line) && line == "async");
  buffer.clear();
  assert(!buffer.pop(line));

  BleLineBuffer bounded(16, 1);
  assert(!bounded.append(reinterpret_cast<const uint8_t*>("one\ntwo\n"), 8));
  assert(bounded.pop(line) && line == "one");
  assert(!bounded.pop(line));
  return 0;
}
