#pragma once

#include <cstddef>

namespace NightKitePlayback {

static constexpr int AUTOPLAY_INTERVALS[] = {1, 5, 10, 20, 30, 60, 120, 300};
static constexpr size_t AUTOPLAY_INTERVAL_COUNT =
    sizeof(AUTOPLAY_INTERVALS) / sizeof(AUTOPLAY_INTERVALS[0]);

static constexpr const char* PLAY_MODES[] = {"manual", "autoplay", "sync"};
static constexpr size_t PLAY_MODE_COUNT = sizeof(PLAY_MODES) / sizeof(PLAY_MODES[0]);

static constexpr const char* BOOT_MODES[] = {"last", "manual", "autoplay", "sync"};
static constexpr size_t BOOT_MODE_COUNT = sizeof(BOOT_MODES) / sizeof(BOOT_MODES[0]);

}  // namespace NightKitePlayback
