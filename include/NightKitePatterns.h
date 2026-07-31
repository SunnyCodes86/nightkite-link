#pragma once

#include <cstddef>
#include <cstdint>

namespace NightKitePatterns {

constexpr const char* NAMES[] = {
    "", "Rainbow", "Full color", "Motion bright", "Runner fixed", "Runner reactive", "Runner dual",
    "Heartbeat", "Ping pong", "Comet swarm", "Breath storm", "Jerk wave", "Yaw spinner", "Yaw circle",
    "Runner dual inv", "Palette beat", "Pacifica kite", "Twinkle motion", "Fire jet", "Noise ring",
    "Pride yaw", "Confetti jerk", "Center ripple", "Audio Pulse Angle Color", "Audio Spectrum Ribbon",
    "Audio Beat Ripples", "Audio Band Comets", "Audio Beat Mosaic",
};
constexpr int COUNT = static_cast<int>(sizeof(NAMES) / sizeof(NAMES[0])) - 1;
constexpr uint32_t ALL_MASK = (1UL << COUNT) - 1UL;

inline const char* name(int id) { return id >= 1 && id <= COUNT ? NAMES[id] : "Unknown"; }

static_assert(COUNT == 27, "Pattern catalog must match current NightKite Multi firmware");

}  // namespace NightKitePatterns
