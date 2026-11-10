#include "emu/Console.hpp"

#include <filesystem>
#include <system_error>

#if defined(EQX_FAKE_EMU)
#include <fstream>

#include "emu/RunnerManifest.hpp"
#endif

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
#if defined(EQX_FAKE_EMU)
        // Desktop testing only: no runner repo, no thumbnails, no ScreenScraper id.
        {"fake",
         "Console Falso",
         {".fake"},
         "",
         "",
         1,
         0x5a2f3a,
         0x2a1520,
         "FAKE",
         0xf0a8b5},
#endif
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

#if defined(EQX_FAKE_EMU)
void seedFakeConsole() {
    const Console* fake = findConsole("fake");
    if (!fake) return;
    const ConsolePaths paths = pathsFor(*fake);
    if (!ensureFolders(paths)) return;
    auto touch = [](const std::string& path, const std::string& body) {
        std::error_code ec;
        if (std::filesystem::exists(path, ec)) return;
        std::ofstream(path, std::ios::binary) << body;
    };
    touch(paths.runner, "fake runner");
    if (!readInstalled(paths.installed)) writeInstalled(paths.installed, Installed{"0.0.0-fake", "", 1, 0});
    for (const char* title : {"Aventura no Vale", "Corrida Turbo", "Castelo Sombrio", "Puzzle Estelar",
                              "Lutadores da Rua", "Nave Cometa", "Fazenda Feliz", "Ninja das Sombras"})
        touch(paths.roms + "/" + title + ".fake", std::string("fake rom: ") + title);
}
#endif

}  // namespace emu
