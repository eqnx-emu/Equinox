#pragma once

#include <string>
#include <vector>

#include "emu/Console.hpp"

namespace emu {

// fileName is the path under the console's folder, with '/' between folders
// ("Crash Bandicoot (USA)/Crash Bandicoot (USA).cue"); title is its last
// part without the extension.
struct RomEntry {
    std::string path, fileName, title;
};

// Folders below the console's that are looked into: one per game, or a few
// levels to group them ("Hacks/", "USA/RPG/").
inline constexpr int kRomFolderDepth = 3;

// Never throws. Regular, non-empty, non-hidden files in `dir` and its folders
// whose extension the console accepts, sorted case-insensitively. Discs an .m3u
// lists beside it are left out: the .m3u is the game.
std::vector<RomEntry> scanRoms(const std::string& dir, const Console& console);

// The name is one scanRoms could list (not hidden, an accepted extension).
bool romNameMatches(const std::string& name, const Console& console);
RomEntry romEntryFor(const std::string& dir, const std::string& name);
// The file's own name, without the folders.
inline std::string romBaseName(const std::string& fileName) {
    const size_t slash = fileName.rfind('/');
    return slash == std::string::npos ? fileName : fileName.substr(slash + 1);
}
// The order scanRoms sorts file names in: by the file's own name, so a game in
// a folder sits among the others.
bool romFileNameLess(const std::string& a, const std::string& b);
// Regular and not empty.
bool romFileUsable(const std::string& path);
// Only the matching names, without asking the card about each file, so it is
// quick on a big folder. False when a folder could not be read to the end.
bool listRomNames(const std::string& dir, const Console& console, std::vector<std::string>& out);

// How many games the last look at the folder found, so the home screen can
// say it without walking a folder that may hold thousands. -1 when never looked.
int readRomCount(const ConsolePaths& paths);
void writeRomCount(const ConsolePaths& paths, int count);

}  // namespace emu
