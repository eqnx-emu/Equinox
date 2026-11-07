#pragma once

#include <string>
#include <vector>

namespace emu {

struct Console {
    std::string slug;
    std::string name;
    std::vector<std::string> extensions;
    std::string libretroSystem;
    std::string repo;
    int protocol;
    unsigned cardColorTop, cardColorBottom;
};

struct ConsolePaths {
    std::string roms, saves, states, covers, emulator, runner, installed;
};

const std::vector<Console>& consoles();
const Console* findConsole(const std::string& slug);
ConsolePaths pathsFor(const Console& console, const std::string& root = "sdmc:/equinox");
// Creates every folder; true when all of them exist afterwards.
bool ensureFolders(const ConsolePaths& paths);

}  // namespace emu
