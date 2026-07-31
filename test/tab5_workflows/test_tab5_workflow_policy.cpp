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
  return 0;
}
