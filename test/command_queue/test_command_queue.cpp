#include <assert.h>

#include <string>
#include <vector>

#include "CommandQueuePolicy.h"

struct Entry {
  std::string command;
  uint32_t generation = 1;
  CommandClass commandClass = CommandClass::User;
  LiveCommandKind liveKind = LiveCommandKind::None;
};

static Entry entry(const char* command, CommandClass commandClass = CommandClass::User,
                   LiveCommandKind liveKind = LiveCommandKind::None)
{
  Entry value;
  value.command = command;
  value.commandClass = commandClass;
  value.liveKind = liveKind;
  return value;
}

static void testLiveCoalescingPreservesBarriers()
{
  std::vector<Entry> queue = {entry("brightness=95", CommandClass::User, LiveCommandKind::Brightness),
                              entry("status", CommandClass::Poll)};
  assert(replaceQueuedLiveCommand(queue, std::string("brightness=255"), LiveCommandKind::Brightness, 1));
  assert(queue.size() == 2 && queue[0].command == "brightness=255");

  queue.push_back(entry("pattern=2", CommandClass::User, LiveCommandKind::Pattern));
  assert(!replaceQueuedLiveCommand(queue, std::string("brightness=127"), LiveCommandKind::Brightness, 1));
  assert(queue[0].command == "brightness=255");
}

static void testCapacityProtectsUserCommands()
{
  std::vector<Entry> queue = {entry("set-a"), entry("poll", CommandClass::Poll)};
  assert(makeCommandQueueSpace(queue, 2, CommandClass::User));
  assert(queue.size() == 1 && queue[0].command == "set-a");

  queue.push_back(entry("set-b"));
  assert(!makeCommandQueueSpace(queue, 2, CommandClass::User));
  assert(!makeCommandQueueSpace(queue, 2, CommandClass::Poll));
  assert(queue.size() == 2);
}

static void testBackgroundDeduplication()
{
  std::vector<Entry> queue = {entry("status", CommandClass::Poll)};
  assert(hasQueuedBackgroundCommand(queue, std::string("status"), CommandClass::Poll, 1));
  assert(!hasQueuedBackgroundCommand(queue, std::string("status"), CommandClass::User, 1));
}

static void testOperationAggregatesPartialFailure()
{
  CommandOperationState operation;
  operation.commandQueued(CommandClass::User);
  operation.commandQueued(CommandClass::User);
  operation.commandQueued(CommandClass::Poll);
  operation.commandFinished(CommandClass::User, false);
  operation.commandFinished(CommandClass::Poll, false);
  operation.commandFinished(CommandClass::User, true);
  bool success = true;
  assert(operation.takeResult(success));
  assert(!success);

  operation.commandQueued(CommandClass::User);
  operation.commandFinished(CommandClass::User, true);
  assert(operation.takeResult(success) && success);

  operation.commandRejected(CommandClass::User);
  assert(operation.takeResult(success) && !success);
}

int main()
{
  testLiveCoalescingPreservesBarriers();
  testCapacityProtectsUserCommands();
  testBackgroundDeduplication();
  testOperationAggregatesPartialFailure();
  return 0;
}
