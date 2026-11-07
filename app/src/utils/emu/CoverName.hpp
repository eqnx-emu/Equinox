#pragma once

#include <string>
#include <vector>

#include "emu/Console.hpp"

namespace emu {

// Libretro thumbnail names to try, best first, without repeats. Characters
// that libretro replaces with '_' are already replaced.
std::vector<std::string> coverNameCandidates(const std::string& title);
std::string coverUrl(const Console& console, const std::string& candidate);
std::string coverPath(const ConsolePaths& paths, const std::string& title);  // <covers>/<title>.png

}  // namespace emu
