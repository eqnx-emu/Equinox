#pragma once

#include <optional>

#include "emu/Console.hpp"
#include "emu/LaunchLine.hpp"
#include "emu/RomScan.hpp"

namespace emu {

// Records the game as played and hands the Switch over to the console's
// runner, which opens Equinox again with a resume token when the player
// leaves. Returns false, after telling the player why, when the game cannot
// be opened (no runner, the ROM is gone, no homebrew loader to chain to);
// Equinox then keeps running. Switch only: elsewhere it always fails.
bool launchRom(const Console& console, const RomEntry& rom, Origin origin, int filter);

// Resume token handed over on the command line by the runner, read in main()
// and consumed by the home screen once it shows.
void setPendingResume(const ResumeToken& token);
std::optional<ResumeToken> takePendingResume();

}  // namespace emu
