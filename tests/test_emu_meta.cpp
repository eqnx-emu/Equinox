#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include "emu/GameMeta.hpp"
#include "emu/PathUtf8.hpp"

namespace fs = std::filesystem;

static fs::path tempDir(const char* name) {
    fs::path dir = fs::temp_directory_path() / name;
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

static void writeFile(const fs::path& path, const std::string& content) {
    std::ofstream(path, std::ios::binary) << content;
}

TEST_CASE("metaPath puts the title in the meta folder") {
    emu::ConsolePaths paths;
    paths.meta = "sdmc:/equinox/meta/snes";
    CHECK(emu::metaPath(paths, "Super Metroid") == "sdmc:/equinox/meta/snes/Super Metroid.json");
}

TEST_CASE("saveMeta then loadMeta round trips every field") {
    const fs::path dir = tempDir("equinox_meta_roundtrip");
    const std::string path = emu::toUtf8(dir / "Chrono Trigger.json");

    emu::GameMeta in;
    in.size = 4194304;
    in.mtime = 1762500000;
    in.identified = true;
    in.crc = 0xb19ed489;
    in.canonical = "Super Metroid (Japan, USA) (En,Ja)";
    in.snap = emu::PartState::Ok;
    in.title = emu::PartState::Missing;
    in.synopsis = emu::PartState::Skipped;
    in.synopsisText = "Samus volta a Zebes: ação, exploração e \"emoção\".";
    in.synopsisLang = "pt";
    REQUIRE(emu::saveMeta(path, in));

    emu::GameMeta out;
    REQUIRE(emu::loadMeta(path, out));
    CHECK(out.size == in.size);
    CHECK(out.mtime == in.mtime);
    CHECK(out.identified);
    CHECK(out.crc == 0xb19ed489);
    CHECK(out.canonical == in.canonical);
    CHECK(out.snap == emu::PartState::Ok);
    CHECK(out.title == emu::PartState::Missing);
    CHECK(out.synopsis == emu::PartState::Skipped);
    CHECK(out.synopsisText == in.synopsisText);
    CHECK(out.synopsisLang == "pt");
}

TEST_CASE("the file stores the crc as eight hex digits") {
    const fs::path dir = tempDir("equinox_meta_crc");
    const std::string path = emu::toUtf8(dir / "a.json");
    emu::GameMeta in;
    in.crc = 0x00000a1f;
    REQUIRE(emu::saveMeta(path, in));
    std::ifstream file(dir / "a.json");
    const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    CHECK(text.find("\"crc\":\"00000a1f\"") != std::string::npos);
    CHECK(text.find("\"v\":1") != std::string::npos);
}

TEST_CASE("loadMeta rejects a missing file, another version and broken JSON") {
    const fs::path dir = tempDir("equinox_meta_reject");
    emu::GameMeta out;
    CHECK_FALSE(emu::loadMeta(emu::toUtf8(dir / "none.json"), out));

    writeFile(dir / "v2.json", R"({"v":2,"size":1,"mtime":1})");
    CHECK_FALSE(emu::loadMeta(emu::toUtf8(dir / "v2.json"), out));

    writeFile(dir / "nov.json", R"({"size":1,"mtime":1})");
    CHECK_FALSE(emu::loadMeta(emu::toUtf8(dir / "nov.json"), out));

    writeFile(dir / "broken.json", R"({"v":1,"size":)");
    CHECK_FALSE(emu::loadMeta(emu::toUtf8(dir / "broken.json"), out));
}

TEST_CASE("saveMeta leaves no .part behind and overwrites an old file") {
    const fs::path dir = tempDir("equinox_meta_part");
    const std::string path = emu::toUtf8(dir / "g.json");
    emu::GameMeta meta;
    meta.canonical = "first";
    REQUIRE(emu::saveMeta(path, meta));
    meta.canonical = "second";
    REQUIRE(emu::saveMeta(path, meta));
    CHECK_FALSE(fs::exists(dir / "g.json.part"));
    emu::GameMeta out;
    REQUIRE(emu::loadMeta(path, out));
    CHECK(out.canonical == "second");
}

TEST_CASE("saveMeta creates the meta folder") {
    const fs::path dir = tempDir("equinox_meta_mkdir");
    CHECK(emu::saveMeta(emu::toUtf8(dir / "new" / "g.json"), emu::GameMeta{}));
}

TEST_CASE("metaFresh is false once the ROM changed") {
    const fs::path dir = tempDir("equinox_meta_fresh");
    const fs::path rom = dir / "game.sfc";
    writeFile(rom, std::string(1024, 'a'));
    const std::string romPath = emu::toUtf8(rom);

    emu::GameMeta meta;
    REQUIRE(emu::romStat(romPath, meta.size, meta.mtime));
    CHECK(emu::metaFresh(meta, meta.size, meta.mtime));

    writeFile(rom, std::string(2048, 'b'));
    uint64_t size = 0;
    int64_t mtime = 0;
    REQUIRE(emu::romStat(romPath, size, mtime));
    CHECK_FALSE(emu::metaFresh(meta, size, mtime));

    // Same size, newer file.
    writeFile(rom, std::string(1024, 'c'));
    fs::last_write_time(rom, fs::last_write_time(rom) + std::chrono::hours(1));
    REQUIRE(emu::romStat(romPath, size, mtime));
    CHECK(size == meta.size);
    CHECK_FALSE(emu::metaFresh(meta, size, mtime));
}

TEST_CASE("romStat fails for a missing ROM") {
    uint64_t size = 0;
    int64_t mtime = 0;
    CHECK_FALSE(emu::romStat("no/such/rom.sfc", size, mtime));
}
