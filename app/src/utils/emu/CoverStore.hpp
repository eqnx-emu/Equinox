#pragma once

#include <string>

#include "emu/Console.hpp"

namespace emu {

// Covers that libretro does not have are remembered in <covers>/.miss/<title>,
// a file holding the Equinox version that looked. A newer Equinox (which may
// try other names) looks again.
std::string coverMissDir(const ConsolePaths& paths);
std::string coverMissPath(const ConsolePaths& paths, const std::string& title);
bool coverMissed(const ConsolePaths& paths, const std::string& title, const std::string& version);
bool markCoverMissed(const ConsolePaths& paths, const std::string& title, const std::string& version);
// One game's miss, when its data brought a name libretro has not been asked for yet.
void clearCoverMiss(const ConsolePaths& paths, const std::string& title);
// "Look for games again": every cover is tried once more.
void clearCoverMisses(const ConsolePaths& paths);

// True when the file starts with the PNG signature.
bool looksLikePng(const std::string& path);

// Puts `from` at `to`. FAT will not rename over an existing file, so `to` is
// removed first when the plain rename fails.
bool replaceFile(const std::string& from, const std::string& to);

}  // namespace emu
