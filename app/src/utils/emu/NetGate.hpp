#pragma once

namespace emu {

// One holder at a time across the whole app: a cover or game-data download
// touching the network and the SD card, or a launch handing over (kept until
// the app quits). Only ever taken with netTryAcquire(): nothing waits on it,
// so no pool thread blocks. Callers that find it busy retry later.
bool netTryAcquire();
void netRelease();

}  // namespace emu
