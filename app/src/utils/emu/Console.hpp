#pragma once

#include <string>
#include "emu/DataRoot.hpp"
#include <vector>

namespace emu {

struct Console {
    std::string slug;
    std::string name;
    std::vector<std::string> extensions;
    std::string libretroSystem;
    std::string repo;
    int protocol = 0;
    unsigned cardColorTop = 0, cardColorBottom = 0;
    // Tag on the card ("SNES") and its color.
    std::string shortName;
    unsigned cardAccent = 0;
};

struct ConsolePaths {
    std::string roms, saves, states, covers, emulator, runner, installed;
    std::string snaps, titles, meta;
};

const std::vector<Console>& consoles();
const Console* findConsole(const std::string& slug);
ConsolePaths pathsFor(const Console& console, const std::string& root = kDataRoot);
// Creates every folder; true when all of them exist afterwards.
bool ensureFolders(const ConsolePaths& paths);

#if defined(EQX_FAKE_EMU)
// Desktop testing: the fake console's runner and a few empty games, so its
// screens open. Nothing is downloaded for it and its games open nothing.
void seedFakeConsole();
#endif

}  // namespace emu
