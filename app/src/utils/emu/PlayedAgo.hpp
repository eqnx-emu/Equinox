#pragma once

#include <cstdint>

namespace emu {

// Calendar days, in local time, from `played` to `now`: 0 for today, 1 for
// yesterday. -1 when the game was never played (played <= 0). A time in the
// future counts as today.
int daysSincePlayed(int64_t played, int64_t now);

}  // namespace emu
