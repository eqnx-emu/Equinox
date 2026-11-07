#include "emu/Console.hpp"

#include <filesystem>
#include <system_error>

namespace emu {

const std::vector<Console>& consoles() {
    static const std::vector<Console> all = {
        {"snes",
         "Super Nintendo",
         {".sfc", ".smc", ".zip"},
         "Nintendo - Super Nintendo Entertainment System",
         "luizfbalves/equinox-snes9x",
         1,
         0x3a2f52,
         0x1b1828,
         "SNES",
         0xb5a8e0},
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
    return p;
}

bool ensureFolders(const ConsolePaths& paths) {
    namespace fs = std::filesystem;
    bool ok = true;
    for (const std::string* dir : {&paths.roms, &paths.saves, &paths.states, &paths.covers, &paths.emulator}) {
        std::error_code ec;
        fs::create_directories(fs::path(*dir), ec);
        if (!fs::is_directory(fs::path(*dir), ec)) ok = false;
    }
    return ok;
}

}  // namespace emu
