#include <assert.h>

#include <fstream>
#include <sstream>
#include <string>

#include "ProfileCodec.h"

namespace {

bool decode(const std::string& json, ProfileData& output, std::string& error)
{
  ProfileData fallback;
  fallback.brightness = 95;
  fallback.stripLength = 25;
  fallback.activePattern = 2;
  fallback.smoothing = 40;
  fallback.accelRange = 4;
  fallback.gyroRange = 500;
  fallback.enabledPatternMask = 6;
  fallback.invertedPatternMask = 2;
  fallback.autoplayIntervalSeconds = 10;
  return decodeProfileJson(json, fallback, output, error);
}

void expectInvalid(const char* json)
{
  ProfileData output;
  std::string error;
  assert(!decode(json, output, error));
  assert(!error.empty());
}

}  // namespace

int main(int argc, char** argv)
{
  assert(argc == 2);
  std::ifstream fixtureFile(argv[1]);
  std::stringstream fixtureBuffer;
  fixtureBuffer << fixtureFile.rdbuf();
  ProfileData fixture;
  std::string error;
  assert(decode(fixtureBuffer.str(), fixture, error));
  assert(fixture.brightness == 159);
  assert(fixture.autoplayEnabled);
  assert(fixture.autoplayIntervalSeconds == 20);
  assert(fixture.enabledPatternMask == 1);
  assert(fixture.invertedPatternMask == 0);

  std::string encoded;
  ProfileData roundTrip;
  assert(encodeProfileJson(fixture, encoded, error));
  assert(decode(encoded, roundTrip, error));
  assert(roundTrip.deviceName == fixture.deviceName);
  assert(roundTrip.enabledPatternMask == fixture.enabledPatternMask);
  assert(roundTrip.syncRole == fixture.syncRole);

  ProfileData compact;
  assert(decode(R"({"profile_version":2,"settings":{"enabled_pattern_mask":5,"inverted_pattern_mask":4,
                "patterns":[{"id":1,"cycle_enabled":false,"inverted":false}]}})", compact, error));
  assert(compact.enabledPatternMask == 5);
  assert(compact.invertedPatternMask == 4);

  ProfileData legacy;
  assert(decode(R"({"profile_version":1,"settings":{"brightness":223,"autoplay_enabled":true,
                "autoplay_interval":60}})", legacy, error));
  assert(legacy.brightness == 223);
  assert(legacy.autoplayEnabled);
  assert(legacy.autoplayIntervalSeconds == 60);
  assert(legacy.stripLength == 25);
  assert(legacy.enabledPatternMask == 6);

  expectInvalid(R"({"profile_version":3,"settings":{}})");
  expectInvalid(R"({"profile_version":2,"project":"Other","settings":{}})");
  expectInvalid(R"({"profile_version":2,"target":"Other","settings":{}})");
  expectInvalid(R"({"profile_version":2,"settings":{"brightness":100}})");
  expectInvalid(R"({"profile_version":2,"settings":{"sync_enabled":"yes"}})");
  expectInvalid(R"({"profile_version":2,"settings":{"enabled_pattern_mask":268435456}})");
  expectInvalid(R"({"profile_version":2,"settings":{"patterns":[{"id":1},{"id":1}]}})");
  expectInvalid(R"({"profile_version":2,"settings":)");
  expectInvalid(R"([])");
  return 0;
}
