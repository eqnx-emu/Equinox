#pragma once

#include <string>
#include <vector>

#include "emu/Console.hpp"

namespace emu {

struct RomEntry {
    std::string path, fileName, title;  // title = file name without extension
};

// Never throws. Regular, non-empty, non-hidden files directly in `dir`
// whose extension the console accepts, sorted case-insensitively.
std::vector<RomEntry> scanRoms(const std::string& dir, const Console& console);

// The name is one scanRoms could list (not hidden, an accepted extension).
bool romNameMatches(const std::string& name, const Console& console);
RomEntry romEntryFor(const std::string& dir, const std::string& name);
// The order scanRoms sorts file names in.
bool romFileNameLess(const std::string& a, const std::string& b);
// Regular and not empty.
bool romFileUsable(const std::string& path);
// Only the matching names, without asking the card about each file, so it is
// quick on a big folder. False when the folder could not be read to the end.
bool listRomNames(const std::string& dir, const Console& console, std::vector<std::string>& out);

// How many games the last look at the folder found, so the home screen can
// say it without walking a folder that may hold thousands. -1 when never looked.
int readRomCount(const ConsolePaths& paths);
void writeRomCount(const ConsolePaths& paths, int count);

}  // namespace emu
