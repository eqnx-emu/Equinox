#include "emu/Console.hpp"

#include <filesystem>
#include <system_error>

namespace emu {

const std::vector<Console>& consoles() {
    static const std::vector<Console> all = {
        {"snes",
         "Super Nintendo",
         {".sfc", ".smc", ".zip", ".7z"},
         "Nintendo - Super Nintendo Entertainment System",
         "eqnx-emu/equinox-snes9x",
         1,
         0x3a2f52,
         0x1b1828,
         "SNES",
         0xb5a8e0},
        {"gba",
         "Game Boy Advance",
         {".gba", ".zip", ".7z"},
         "Nintendo - Game Boy Advance",
         "eqnx-emu/equinox-mgba",
         1,
         0x34346e,
         0x171733,
         "GBA",
         0xa6a8f0},
        {"gbc",
         "Game Boy Color",
         {".gbc", ".zip", ".7z"},
         "Nintendo - Game Boy Color",
         "eqnx-emu/equinox-gambatte",
         1,
         0x1f5a56,
         0x0f2a2b,
         "GBC",
         0x7fd6c4},
    };
    return all;
}

const Console* findConsole(const std::string& slug) {
    for (const Console& c : consoles())
        if (c.slug == slug) return &c;
    return nullptr;
}

ConsolePaths pathsFor(const Console& console, const std::string& root) {
    ConsolePaths p;
    p.roms = root + "/roms/" + console.slug;
    p.saves = root + "/saves/" + console.slug;
    p.states = root + "/states/" + console.slug;
    p.covers = root + "/covers/" + console.slug;
    p.emulator = root + "/emulators/" + console.slug;
    p.runner = p.emulator + "/runner.bin";
    p.installed = p.emulator + "/installed.json";
    p.snaps = root + "/snaps/" + console.slug;
    p.titles = root + "/titles/" + console.slug;
    p.meta = root + "/meta/" + console.slug;
    return p;
}

bool ensureFolders(const ConsolePaths& paths) {
    namespace fs = std::filesystem;
    bool ok = true;
    for (const std::string* dir : {&paths.roms, &paths.saves, &paths.states, &paths.covers, &paths.emulator,
                                   &paths.snaps, &paths.titles, &paths.meta}) {
        std::error_code ec;
        fs::create_directories(fs::path(*dir), ec);
        if (!fs::is_directory(fs::path(*dir), ec)) ok = false;
    }
    return ok;
}

}  // namespace emu
