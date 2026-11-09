#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include <filesystem>
#include <fstream>
#include <string>

#include "Updater.hpp"

namespace fs = std::filesystem;

namespace {

// A fresh folder with the running NRO's path in it.
std::string freshTarget() {
    const fs::path dir = fs::temp_directory_path() / "eqx_test_updater";
    fs::remove_all(dir);
    fs::create_directories(dir);
    return (dir / "Equinox.nro").string();
}

void writeFile(const std::string& path, const std::string& body) {
    std::ofstream(path, std::ios::binary) << body;
}

std::string nro(size_t size = 64) {
    std::string body(size, '\0');
    body.replace(0x10, 4, "NRO0");
    return body;
}

bool exists(const std::string& path) { return fs::exists(path); }

}  // namespace

TEST_CASE("isNewer compares dotted versions with an optional v") {
    CHECK(Updater::isNewer("v1.5.6", "1.5.5"));
    CHECK(Updater::isNewer("1.6", "1.5.9"));
    CHECK_FALSE(Updater::isNewer("1.5.5", "v1.5.5"));
    CHECK_FALSE(Updater::isNewer("1.5", "1.5.0"));
    CHECK_FALSE(Updater::isNewer("garbage", "1.0"));
}

TEST_CASE("the version file sits next to the staged NRO") {
    CHECK(Updater::stagedVersionPath("sdmc:/switch/Equinox.nro") ==
          "sdmc:/switch/Equinox.nro.new.version");
}

TEST_CASE("looksLikeNro wants the magic and the advertised size") {
    const std::string target = freshTarget();
    writeFile(target, nro(64));
    CHECK(Updater::looksLikeNro(target, 64));
    CHECK(Updater::looksLikeNro(target, 0));
    CHECK_FALSE(Updater::looksLikeNro(target, 65));
    writeFile(target, std::string(64, 'x'));
    CHECK_FALSE(Updater::looksLikeNro(target, 64));
    CHECK_FALSE(Updater::looksLikeNro(target + ".missing", 0));
}

TEST_CASE("stage moves the part into place, then writes the version") {
    const std::string target = freshTarget();
    writeFile(target + ".new.part", nro());
    REQUIRE(Updater::stage(target, "2.0.0"));
    CHECK_FALSE(exists(target + ".new.part"));
    CHECK(exists(target + ".new"));
    CHECK(Updater::stagedUpdate(target, "1.0.0") == std::optional<std::string>("2.0.0"));
    // Still there: "Later" keeps it for the next start.
    CHECK(exists(target + ".new"));
    CHECK(exists(Updater::stagedVersionPath(target)));
}

TEST_CASE("stage replaces an earlier staged update") {
    const std::string target = freshTarget();
    writeFile(target + ".new", nro());
    writeFile(Updater::stagedVersionPath(target), "1.5.0");
    writeFile(target + ".new.part", nro());
    REQUIRE(Updater::stage(target, "2.0.0"));
    CHECK(Updater::stagedUpdate(target, "1.0.0") == std::optional<std::string>("2.0.0"));
}

TEST_CASE("stage without a part stages nothing") {
    const std::string target = freshTarget();
    CHECK_FALSE(Updater::stage(target, "2.0.0"));
    CHECK_FALSE(exists(target + ".new"));
    CHECK_FALSE(exists(Updater::stagedVersionPath(target)));
}

TEST_CASE("nothing staged is not an update") {
    const std::string target = freshTarget();
    CHECK_FALSE(Updater::stagedUpdate(target, "1.0.0"));
}

TEST_CASE("a staged version not newer than the running one is removed") {
    const std::string target = freshTarget();
    writeFile(target + ".new", nro());
    writeFile(Updater::stagedVersionPath(target), "1.0.0\n");
    CHECK_FALSE(Updater::stagedUpdate(target, "1.0.0"));
    CHECK_FALSE(exists(target + ".new"));
    CHECK_FALSE(exists(Updater::stagedVersionPath(target)));
}

TEST_CASE("a .new without its version file is not staged and goes") {
    const std::string target = freshTarget();
    writeFile(target + ".new", nro());
    CHECK_FALSE(Updater::stagedUpdate(target, "1.0.0"));
    CHECK_FALSE(exists(target + ".new"));
}

TEST_CASE("a version file without its .new goes") {
    const std::string target = freshTarget();
    writeFile(Updater::stagedVersionPath(target), "2.0.0");
    CHECK_FALSE(Updater::stagedUpdate(target, "1.0.0"));
    CHECK_FALSE(exists(Updater::stagedVersionPath(target)));
}

TEST_CASE("a staged file that is not an NRO goes") {
    const std::string target = freshTarget();
    writeFile(target + ".new", "not an nro");
    writeFile(Updater::stagedVersionPath(target), "2.0.0");
    CHECK_FALSE(Updater::stagedUpdate(target, "1.0.0"));
    CHECK_FALSE(exists(target + ".new"));
    CHECK_FALSE(exists(Updater::stagedVersionPath(target)));
}

TEST_CASE("an unreadable version goes") {
    const std::string target = freshTarget();
    writeFile(target + ".new", nro());
    writeFile(Updater::stagedVersionPath(target), "");
    CHECK_FALSE(Updater::stagedUpdate(target, "1.0.0"));
    CHECK_FALSE(exists(target + ".new"));
}
