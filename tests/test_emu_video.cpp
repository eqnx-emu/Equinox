#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include <filesystem>
#include <fstream>
#include <string>

#include "emu/Console.hpp"
#include "emu/VideoDefaults.hpp"

using namespace emu;

namespace {

std::string tempFile(const char* name) {
    std::filesystem::path dir = std::filesystem::temp_directory_path() / "equinox_test_video";
    std::filesystem::create_directories(dir);
    std::filesystem::path p = dir / name;
    std::filesystem::remove(p);
    return p.string();
}

void put(const std::string& path, const std::string& text) {
    std::ofstream(path, std::ios::binary) << text;
}

void checkDefaults(const VideoDefaults& v) {
    CHECK(v.size == "original");
    CHECK(v.smooth == "pixel");
    CHECK(v.filter == "none");
    CHECK(v.border == "equinox");
}

}  // namespace

TEST_CASE("videoDefaultsPath") {
    const Console* snes = findConsole("snes");
    REQUIRE(snes != nullptr);
    CHECK(videoDefaultsPath(pathsFor(*snes)) == "sdmc:/equinox/emulators/snes/video.json");
}

TEST_CASE("write then read round trip") {
    const std::string path = tempFile("roundtrip.json");
    VideoDefaults v;
    v.size = "4:3";
    v.smooth = "soft";
    v.filter = "ntsc";
    v.border = "tv";
    REQUIRE(writeVideoDefaults(path, v));
    VideoDefaults r = readVideoDefaults(path);
    CHECK(r.size == "4:3");
    CHECK(r.smooth == "soft");
    CHECK(r.filter == "ntsc");
    CHECK(r.border == "tv");
    CHECK_FALSE(std::filesystem::exists(path + ".tmp"));

    v.size = "full";
    REQUIRE(writeVideoDefaults(path, v));  // overwrite an existing file
    CHECK(readVideoDefaults(path).size == "full");
}

TEST_CASE("bad files give defaults") {
    checkDefaults(readVideoDefaults(tempFile("missing.json")));
    const std::string path = tempFile("bad.json");
    put(path, "");
    checkDefaults(readVideoDefaults(path));
    put(path, "[]");
    checkDefaults(readVideoDefaults(path));
    put(path, "{\"size\":\"4:3\",\"smo");
    checkDefaults(readVideoDefaults(path));
}

TEST_CASE("unknown values fall back per field") {
    const std::string path = tempFile("partial.json");
    put(path, "{\"size\":\"huge\",\"smooth\":\"soft\",\"filter\":7,\"border\":\"black\"}");
    VideoDefaults v = readVideoDefaults(path);
    CHECK(v.size == "original");
    CHECK(v.smooth == "soft");
    CHECK(v.filter == "none");
    CHECK(v.border == "black");
}

TEST_CASE("menu id lists") {
    CHECK(kSizeIds == std::vector<std::string>{"original", "8:7", "4:3", "full"});
    CHECK(kSmoothIds == std::vector<std::string>{"pixel", "sharp", "soft"});
    CHECK(kFilterIds == std::vector<std::string>{"none", "scanlines", "ntsc"});
    CHECK(kBorderIds == std::vector<std::string>{"equinox", "cover", "tv", "black"});
}
