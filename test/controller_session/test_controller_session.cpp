#include <assert.h>
#include <limits.h>

#include "ControllerSessionPolicy.h"

static void testInitialRefreshRequiresEverySuccessfulPart()
{
  InitialRefreshState refresh;
  assert(!refresh.ready());
  refresh.commandSucceeded(InitialRefreshPart::Info);
  refresh.commandSucceeded(InitialRefreshPart::Status);
  refresh.commandSucceeded(InitialRefreshPart::Config);
  assert(!refresh.ready());
  refresh.commandSucceeded(InitialRefreshPart::Play);
  assert(refresh.ready());
  refresh.reset();
  assert(!refresh.ready());
  refresh.legacyShowSucceeded();
  assert(refresh.ready());
}

static void testSessionGuardRequiresFreshResolvedState()
{
  assert(controllerSessionReady(true, true, true, true));
  assert(!controllerSessionReady(false, true, true, true));
  assert(!controllerSessionReady(true, false, true, true));
  assert(!controllerSessionReady(true, true, false, true));
  assert(!controllerSessionReady(true, true, true, false));
}

static void testBeaconAgeAdvancesAndHandlesWraparound()
{
  assert(currentBeaconAgeMs(250, 1000, 1750) == 1000);
  assert(currentBeaconAgeMs(-1, 1000, 1750) == -1);
  assert(currentBeaconAgeMs(20, UINT32_MAX - 9, 10) == 40);
  assert(currentBeaconAgeMs(INT_MAX - 5, 1, 20) == INT_MAX);
}

int main()
{
  testInitialRefreshRequiresEverySuccessfulPart();
  testSessionGuardRequiresFreshResolvedState();
  testBeaconAgeAdvancesAndHandlesWraparound();
  return 0;
}
