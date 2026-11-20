#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include "emu/Console.hpp"
#include "emu/GameMeta.hpp"
#include "emu/RomScan.hpp"

namespace emu {

// What the game list knows about one game.
struct LibraryGame {
    RomEntry rom;
    bool hasCover = false;
    bool missed = false;  // libretro had no cover, as of this version
    // A meta that still describes the ROM on the card.
    bool hasData = false;
    GameMeta meta;
    std::string snapPath, titlePath;  // empty when the file is not there
};

// <emulator>/library-index.json: the list as it last stood, so opening it does
// not have to look at every game on the card again.
std::string libraryIndexPath(const ConsolePaths& paths);

// False when there is no usable index: missing, damaged, empty, or written by
// another app version (covers missing "as of this version" must be asked again).
bool loadLibraryIndex(const ConsolePaths& paths, const std::string& appVersion,
                      std::vector<LibraryGame>& out);
// Writes <path>.part, then swaps it in. Safe from any thread.
bool saveLibraryIndex(const ConsolePaths& paths, const std::string& appVersion,
                      const std::vector<LibraryGame>& games);

// Covers keep downloading after the list closes, so the index can miss some
// that are on the card. Marks every game without a cover, and not missed
// (those are never asked again), whose file `onCard`
// finds; returns how many changed.
size_t markCoversOnCard(std::vector<LibraryGame>& games,
                        const std::function<bool(const LibraryGame&)>& onCard);

struct LibraryDiff {
    std::vector<std::string> added, removed;  // file names
};
LibraryDiff diffLibrary(std::vector<std::string> known, std::vector<std::string> onCard);

}  // namespace emu
