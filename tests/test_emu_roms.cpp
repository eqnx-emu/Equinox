#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "emu/CoverName.hpp"
#include "emu/PathUtf8.hpp"
#include "emu/RomScan.hpp"

namespace fs = std::filesystem;

static void put(const fs::path& p, const std::string& body) {
    std::ofstream f(p, std::ios::binary);
    f << body;
}

TEST_CASE("scanRoms filters and sorts") {
    const emu::Console* snes = emu::findConsole("snes");
    REQUIRE(snes != nullptr);
    fs::path dir = fs::temp_directory_path() /
                   ("equinox_roms_" +
                    std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(dir / "sub");
    put(dir / "Super Metroid (USA).sfc", "x");
    put(dir / "b.SMC", "x");
    put(dir / "B2.ZIP", "x");
    put(dir / "Super Mario World v1.1.sfc", "x");
    put(dir / emu::fromUtf8("Pok\xC3\xA9mon d'Or.zip"), "x");
    put(dir / "._Super Metroid (USA).sfc", "x");
    put(dir / ".DS_Store", "x");
    put(dir / "notes.txt", "x");
    put(dir / "vazio.sfc", "");
    put(dir / "sub" / "x.sfc", "x");

    auto roms = emu::scanRoms(emu::toUtf8(dir), *snes);
    REQUIRE(roms.size() == 5);
    CHECK(roms[0].fileName == "b.SMC");
    CHECK(roms[0].title == "b");
    CHECK(roms[2].fileName == "Pok\xC3\xA9mon d'Or.zip");
    CHECK(roms[2].title == "Pok\xC3\xA9mon d'Or");
    CHECK(roms[0].title == "b");
    CHECK(roms[1].fileName == "B2.ZIP");
    CHECK(roms[3].title == "Super Mario World v1.1");
    CHECK(roms[4].title == "Super Metroid (USA)");
    CHECK(fs::exists(emu::fromUtf8(roms[2].path)));

    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST_CASE("scanRoms missing dir") {
    const emu::Console* snes = emu::findConsole("snes");
    CHECK(emu::scanRoms("/definitely/not/here/equinox", *snes).empty());
}

TEST_CASE("cover candidates") {
    CHECK(emu::coverNameCandidates("Super Mario World (U) [!]") ==
          std::vector<std::string>{"Super Mario World (U) [!]", "Super Mario World (U)",
                                   "Super Mario World (USA)", "Super Mario World"});
    // libretro always names the region: a plain name gets the common ones.
    CHECK(emu::coverNameCandidates("Super Metroid") ==
          std::vector<std::string>{"Super Metroid", "Super Metroid (USA)",
                                   "Super Metroid (USA, Europe)", "Super Metroid (World)",
                                   "Super Metroid (Europe)", "Super Metroid (Japan)"});
    CHECK(emu::coverNameCandidates("Star Fox: Assault?")[0] == "Star Fox_ Assault_");
    CHECK(emu::coverNameCandidates("Star Fox: Assault?")[1] == "Star Fox_ Assault_ (USA)");
    CHECK(emu::coverNameCandidates("a&b*c/d:e`f<g>h?i\\j|k\"l")[0] ==
          "a_b_c_d_e_f_g_h_i_j_k_l");
    // A title that already names its region gets no guessed ones.
    CHECK(emu::coverNameCandidates("Super Mario World (USA)") ==
          std::vector<std::string>{"Super Mario World (USA)", "Super Mario World"});
}

TEST_CASE("cover url and path") {
    const emu::Console* snes = emu::findConsole("snes");
    CHECK(emu::coverUrl(*snes, "Super Metroid (Japan, USA) (En,Ja)") ==
          "https://thumbnails.libretro.com/Nintendo%20-%20Super%20Nintendo%20Entertainment%20System/"
          "Named_Boxarts/Super%20Metroid%20%28Japan%2C%20USA%29%20%28En%2CJa%29.png");
    CHECK(emu::coverUrl(*snes, "Pok\xC3\xA9").find("Pok%C3%A9.png") != std::string::npos);
    CHECK(emu::coverPath(emu::pathsFor(*snes), "Zelda") == "sdmc:/equinox/covers/snes/Zelda.png");
}
