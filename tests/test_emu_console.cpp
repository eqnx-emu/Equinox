#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include <filesystem>
#include <string>
#include <vector>

#include "emu/Console.hpp"

TEST_CASE("snes paths") {
    const emu::Console* snes = emu::findConsole("snes");
    REQUIRE(snes != nullptr);
    emu::ConsolePaths p = emu::pathsFor(*snes);
    CHECK(p.roms == "sdmc:/equinox/roms/snes");
    CHECK(p.saves == "sdmc:/equinox/saves/snes");
    CHECK(p.states == "sdmc:/equinox/states/snes");
    CHECK(p.covers == "sdmc:/equinox/covers/snes");
    CHECK(p.emulator == "sdmc:/equinox/emulators/snes");
    CHECK(p.runner == "sdmc:/equinox/emulators/snes/runner.bin");
    CHECK(p.installed == "sdmc:/equinox/emulators/snes/installed.json");
    CHECK(p.snaps == "sdmc:/equinox/snaps/snes");
    CHECK(p.titles == "sdmc:/equinox/titles/snes");
    CHECK(p.meta == "sdmc:/equinox/meta/snes");
}

TEST_CASE("console lookup") {
    REQUIRE(emu::findConsole("snes") != nullptr);
    const emu::Console* snes = emu::findConsole("snes");
    CHECK(snes->name == "Super Nintendo");
    CHECK(snes->extensions == std::vector<std::string>{".sfc", ".smc", ".zip", ".7z"});
    CHECK(snes->libretroSystem == "Nintendo - Super Nintendo Entertainment System");
    CHECK(snes->repo == "luizfbalves/equinox-snes9x");
    CHECK(snes->protocol == 1);
    CHECK(snes->cardColorTop == 0x3a2f52);
    CHECK(snes->cardColorBottom == 0x1b1828);
    CHECK(snes->shortName == "SNES");
    CHECK(snes->cardAccent == 0xb5a8e0);
    CHECK(emu::findConsole("nes") == nullptr);
    const emu::Console* gba = emu::findConsole("gba");
    REQUIRE(gba != nullptr);
    CHECK(gba->extensions == std::vector<std::string>{".gba", ".zip", ".7z"});
    CHECK(gba->libretroSystem == "Nintendo - Game Boy Advance");
    CHECK(gba->repo == "luizfbalves/equinox-mgba");
    const emu::Console* gbc = emu::findConsole("gbc");
    REQUIRE(gbc != nullptr);
    CHECK(gbc->extensions == std::vector<std::string>{".gbc", ".zip", ".7z"});
    CHECK(gbc->libretroSystem == "Nintendo - Game Boy Color");
    CHECK(gbc->repo == "luizfbalves/equinox-gambatte");
    CHECK(emu::consoles().size() == 3);
}

TEST_CASE("ensureFolders creates everything and is idempotent") {
    namespace fs = std::filesystem;
    fs::path root = fs::temp_directory_path() / "equinox_emu_console_test";
    std::error_code ec;
    fs::remove_all(root, ec);

    emu::ConsolePaths p = emu::pathsFor(*emu::findConsole("snes"), root.generic_string());
    CHECK(emu::ensureFolders(p));
    CHECK(emu::ensureFolders(p));
    for (const std::string& d : {p.roms, p.saves, p.states, p.covers, p.emulator, p.snaps, p.titles, p.meta})
        CHECK(fs::is_directory(fs::path(d)));

    fs::remove_all(root, ec);
}
