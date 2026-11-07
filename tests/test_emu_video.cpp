#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>

#include <jansson.h>

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

TEST_CASE("an interrupted write is recovered and a failed one keeps the old file") {
    const std::string path = tempFile("recover.json");
    std::filesystem::remove(path + ".bak");
    std::filesystem::remove(path + ".tmp");
    // Killed between moving the old file aside and moving the new one in.
    put(path + ".bak", R"({"size":"8:7"})");
    put(path + ".tmp", R"({"size":"full"})");
    CHECK(readVideoDefaults(path).size == "8:7");
    CHECK(std::filesystem::exists(path));

    // The new file cannot be moved in: the target name is taken by a folder for .bak.
    std::filesystem::remove(path + ".tmp");
    std::filesystem::create_directories(path + ".bak/x");
    VideoDefaults v;
    v.size = "full";
    CHECK_FALSE(writeVideoDefaults(path, v));
    CHECK(readVideoDefaults(path).size == "8:7");
    CHECK_FALSE(std::filesystem::exists(path + ".tmp"));
    std::filesystem::remove_all(path + ".bak");
    REQUIRE(writeVideoDefaults(path, v));
    CHECK(readVideoDefaults(path).size == "full");
    CHECK_FALSE(std::filesystem::exists(path + ".bak"));
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

TEST_CASE("a leading UTF-8 BOM is accepted") {
    const std::string path = tempFile("bom.json");
    put(path, "\xEF\xBB\xBF{\"size\":\"full\"}");
    CHECK(readVideoDefaults(path).size == "full");
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

// emu_video_view labels each id with "emu/video/<row>_<id>", ':' read as '_'.
TEST_CASE("every menu id has a label in every language") {
    const std::pair<const char*, const std::vector<std::string>*> rows[] = {
        {"size", &kSizeIds}, {"smooth", &kSmoothIds}, {"filter", &kFilterIds}, {"border", &kBorderIds}};
    int languages = 0;
    for (const auto& dir : std::filesystem::directory_iterator(EQX_I18N_DIR)) {
        const std::string file = (dir.path() / "main.json").string();
        json_error_t err;
        json_t* root = json_load_file(file.c_str(), 0, &err);
        REQUIRE_MESSAGE(root != nullptr, file);
        json_t* video = json_object_get(json_object_get(root, "emu"), "video");
        for (const auto& [row, ids] : rows) {
            for (std::string id : *ids) {
                std::replace(id.begin(), id.end(), ':', '_');
                const std::string key = std::string(row) + "_" + id;
                CHECK_MESSAGE(json_is_string(json_object_get(video, key.c_str())), file, " ", key);
            }
        }
        json_decref(root);
        ++languages;
    }
    CHECK(languages > 1);
}
