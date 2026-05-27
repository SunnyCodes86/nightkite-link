#include <assert.h>
#include <string.h>

#include "ControllerBatteryParser.h"

static void testLegacyBatteryWithoutPercentOrState()
{
  ControllerBatteryParseResult result;
  assert(parseControllerBatteryLine("OK battery_raw=1510 battery_voltage=3.651 usb_power_raw=0", &result));
  assert(result.hasVoltage);
  assert(result.voltage > 3.65f && result.voltage < 3.66f);
  assert(!result.hasPercent);
  assert(!result.hasState);
}

static void testNewBatteryWithPercentAndState()
{
  ControllerBatteryParseResult result;
  assert(parseControllerBatteryLine(
      "OK battery_raw=1494 battery_voltage=3.620 battery_percent=37 battery_state=LOW_WARNING unknown=kept",
      &result));
  assert(result.hasVoltage);
  assert(result.hasPercent);
  assert(result.percent == 37);
  assert(result.hasState);
  assert(strcmp(result.state, "LOW_WARNING") == 0);
  assert(strcmp(compactControllerBatteryStateLabel(result.state), "LOW") == 0);
}

static void testNk4StatusWithBatteryFields()
{
  ControllerBatteryParseResult result;
  assert(parseControllerBatteryLine(
      "NK4 seq=8 ok pattern=4 brightness=159 battery_percent=6 battery_voltage=3.360 battery_state=CRITICAL fps=120",
      &result));
  assert(result.hasVoltage);
  assert(result.hasPercent);
  assert(result.percent == 6);
  assert(result.hasState);
  assert(strcmp(result.state, "CRITICAL") == 0);
  assert(strcmp(compactControllerBatteryStateLabel(result.state), "CRIT") == 0);
}

int main()
{
  testLegacyBatteryWithoutPercentOrState();
  testNewBatteryWithPercentAndState();
  testNk4StatusWithBatteryFields();
  return 0;
}
