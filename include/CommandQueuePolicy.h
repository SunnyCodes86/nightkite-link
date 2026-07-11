#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

enum class CommandClass : uint8_t {
  User,
  RequiredInit,
  OptionalInit,
  Poll,
};

enum class LiveCommandKind : uint8_t {
  None,
  Brightness,
  Pattern,
};

class CommandOperationState {
public:
  void commandQueued(CommandClass commandClass)
  {
    if (commandClass == CommandClass::User) {
      active = true;
      ++pending;
    }
  }

  void commandRejected(CommandClass commandClass)
  {
    if (commandClass == CommandClass::User) {
      active = true;
      failed = true;
    }
  }

  void commandFinished(CommandClass commandClass, bool success)
  {
    if (commandClass != CommandClass::User || pending == 0) {
      return;
    }
    --pending;
    failed = failed || !success;
  }

  void failRemaining()
  {
    if (active || pending > 0) {
      active = true;
      failed = true;
      pending = 0;
    }
  }

  void abort()
  {
    active = false;
    failed = false;
    pending = 0;
  }

  bool hasFailed() const { return active && failed; }

  bool takeResult(bool& success)
  {
    if (!active || pending != 0) {
      return false;
    }
    success = !failed;
    abort();
    return true;
  }

private:
  size_t pending = 0;
  bool active = false;
  bool failed = false;
};

template <typename Entry, typename Command>
bool replaceQueuedLiveCommand(std::vector<Entry>& queue, const Command& command, LiveCommandKind liveKind,
                              uint32_t generation)
{
  if (liveKind == LiveCommandKind::None) {
    return false;
  }
  for (auto it = queue.rbegin(); it != queue.rend(); ++it) {
    if (it->generation != generation || it->commandClass == CommandClass::RequiredInit) {
      return false;
    }
    if (it->liveKind == liveKind) {
      it->command = command;
      return true;
    }
    if (it->commandClass == CommandClass::User) {
      return false;
    }
  }
  return false;
}

template <typename Entry, typename Command>
bool hasQueuedBackgroundCommand(const std::vector<Entry>& queue, const Command& command, CommandClass commandClass,
                                uint32_t generation)
{
  if (commandClass != CommandClass::OptionalInit && commandClass != CommandClass::Poll) {
    return false;
  }
  for (const auto& entry : queue) {
    if (entry.generation == generation && entry.commandClass == commandClass && entry.command == command) {
      return true;
    }
  }
  return false;
}

template <typename Entry>
bool makeCommandQueueSpace(std::vector<Entry>& queue, size_t capacity, CommandClass incomingClass)
{
  if (queue.size() < capacity) {
    return true;
  }
  if (incomingClass == CommandClass::OptionalInit || incomingClass == CommandClass::Poll) {
    return false;
  }
  for (auto it = queue.begin(); it != queue.end(); ++it) {
    if (it->commandClass == CommandClass::OptionalInit || it->commandClass == CommandClass::Poll) {
      queue.erase(it);
      return true;
    }
  }
  return false;
}
