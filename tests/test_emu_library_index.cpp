#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "emu/LibraryIndex.hpp"
#include "emu/PathUtf8.hpp"

namespace fs = std::filesystem;

static emu::ConsolePaths tempPaths(const char* name) {
    fs::path dir = fs::temp_directory_path() / name;
    fs::remove_all(dir);
    fs::create_directories(dir / "roms");
    emu::ConsolePaths paths;
    paths.emulator = emu::toUtf8(dir);
    paths.roms = emu::toUtf8(dir / "roms");
    return paths;
}

static void touch(const fs::path& path, const std::string& body) {
    std::ofstream file(path, std::ios::binary);
    file << body;
}

TEST_CASE("the index brings back everything the list knows") {
    const emu::ConsolePaths paths = tempPaths("equinox_index_round");
    emu::LibraryGame game;
    game.rom = emu::romEntryFor(paths.roms, "Jogo (U).7z");
    game.hasCover = true;
    game.snapPath = "sdmc:/snap.png";
    game.titlePath = "sdmc:/title.png";
    game.hasData = true;
    game.meta.size = 1234;
    game.meta.mtime = 99;
    game.meta.identified = true;
    game.meta.crc = 0xABCD1234;
    game.meta.canonical = "Jogo (USA)";
    game.meta.snap = emu::PartState::Ok;
    game.meta.synopsisText = "Texto";
    emu::LibraryGame bare;
    bare.rom = emu::romEntryFor(paths.roms, "Outro.sfc");
    bare.missed = true;

    REQUIRE(emu::saveLibraryIndex(paths, "1.0", {game, bare}));
    std::vector<emu::LibraryGame> loaded;
    REQUIRE(emu::loadLibraryIndex(paths, "1.0", loaded));
    REQUIRE(loaded.size() == 2);
    CHECK(loaded[0].rom.path == paths.roms + "/Jogo (U).7z");
    CHECK(loaded[0].rom.title == "Jogo (U)");
    CHECK(loaded[0].hasCover);
    CHECK_FALSE(loaded[0].missed);
    CHECK(loaded[0].snapPath == "sdmc:/snap.png");
    CHECK(loaded[0].titlePath == "sdmc:/title.png");
    CHECK(loaded[0].hasData);
    CHECK(loaded[0].meta.size == 1234);
    CHECK(loaded[0].meta.mtime == 99);
    CHECK(loaded[0].meta.identified);
    CHECK(loaded[0].meta.crc == 0xABCD1234);
    CHECK(loaded[0].meta.canonical == "Jogo (USA)");
    CHECK(loaded[0].meta.snap == emu::PartState::Ok);
    CHECK(loaded[0].meta.synopsisText == "Texto");
    CHECK(loaded[1].missed);
    CHECK_FALSE(loaded[1].hasData);
}

TEST_CASE("an index that cannot be trusted is not used") {
    const emu::ConsolePaths paths = tempPaths("equinox_index_bad");
    std::vector<emu::LibraryGame> loaded;
    CHECK_FALSE(emu::loadLibraryIndex(paths, "1.0", loaded));

    emu::LibraryGame game;
    game.rom = emu::romEntryFor(paths.roms, "Jogo.sfc");
    REQUIRE(emu::saveLibraryIndex(paths, "1.0", {game}));
    CHECK_FALSE(emu::loadLibraryIndex(paths, "2.0", loaded));

    touch(emu::fromUtf8(emu::libraryIndexPath(paths)), "{nope");
    CHECK_FALSE(emu::loadLibraryIndex(paths, "1.0", loaded));

    REQUIRE(emu::saveLibraryIndex(paths, "1.0", {}));
    CHECK_FALSE(emu::loadLibraryIndex(paths, "1.0", loaded));
}

TEST_CASE("the check tells what came and what went") {
    const emu::LibraryDiff diff = emu::diffLibrary({"b.7z", "a.7z", "c.7z"}, {"d.7z", "a.7z", "c.7z"});
    CHECK(diff.added == std::vector<std::string>{"d.7z"});
    CHECK(diff.removed == std::vector<std::string>{"b.7z"});
    const emu::LibraryDiff same = emu::diffLibrary({"a.7z"}, {"a.7z"});
    CHECK(same.added.empty());
    CHECK(same.removed.empty());
}

TEST_CASE("the folder listing gives only names the console accepts") {
    const emu::ConsolePaths paths = tempPaths("equinox_index_list");
    const fs::path roms = emu::fromUtf8(paths.roms);
    touch(roms / "b.7z", "x");
    touch(roms / "a.SFC", "x");
    touch(roms / ".hidden.7z", "x");
    touch(roms / "notes.txt", "x");

    emu::Console console;
    console.extensions = {".sfc", ".7z"};
    std::vector<std::string> names;
    REQUIRE(emu::listRomNames(paths.roms, console, names));
    std::sort(names.begin(), names.end());
    CHECK(names == std::vector<std::string>{"a.SFC", "b.7z"});

    names.clear();
    CHECK_FALSE(emu::listRomNames(paths.roms + "/missing", console, names));
}

TEST_CASE("empty files are not games") {
    const emu::ConsolePaths paths = tempPaths("equinox_index_usable");
    const fs::path roms = emu::fromUtf8(paths.roms);
    touch(roms / "full.7z", "x");
    touch(roms / "empty.7z", "");
    CHECK(emu::romFileUsable(paths.roms + "/full.7z"));
    CHECK_FALSE(emu::romFileUsable(paths.roms + "/empty.7z"));
    CHECK_FALSE(emu::romFileUsable(paths.roms + "/gone.7z"));
}
