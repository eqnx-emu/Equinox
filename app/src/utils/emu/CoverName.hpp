#pragma once

#include <string>
#include <vector>

#include "emu/Console.hpp"

namespace emu {

// Libretro thumbnail names to try, best first, without repeats. Characters
// that libretro replaces with '_' are already replaced.
std::vector<std::string> coverNameCandidates(const std::string& title);
// Percent-encodes everything but A-Z a-z 0-9 - _ . ~
std::string urlEncode(const std::string& s);
std::string coverUrl(const Console& console, const std::string& candidate);
std::string coverPath(const ConsolePaths& paths, const std::string& title);  // <covers>/<title>.png

enum class ThumbKind { Boxart, Snap, Title };
std::string thumbUrl(const Console& console, ThumbKind kind, const std::string& candidate);
std::string thumbPath(const ConsolePaths& paths, ThumbKind kind, const std::string& title);  // covers/, snaps/ or titles/ + <title>.png
// The database's canonical name first (when not empty), then coverNameCandidates(title), without repeats.
std::vector<std::string> thumbNameCandidates(const std::string& canonical, const std::string& title);

}  // namespace emu
