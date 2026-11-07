#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include "emu/CoverStore.hpp"
#include "emu/PathUtf8.hpp"

namespace fs = std::filesystem;

TEST_CASE("cover misses are kept per version and cleared") {
    fs::path dir = fs::temp_directory_path() /
                   ("equinox_covers_" +
                    std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    emu::ConsolePaths paths;
    paths.covers = emu::toUtf8(dir);

    const std::string title = "Pok\xC3\xA9mon d'Or (U) [!]";
    CHECK(emu::coverMissDir(paths) == paths.covers + "/.miss");
    CHECK(emu::coverMissPath(paths, title) == paths.covers + "/.miss/" + title);
    CHECK_FALSE(emu::coverMissed(paths, title, "1.9.0"));

    REQUIRE(emu::markCoverMissed(paths, title, "1.9.0"));
    CHECK(emu::coverMissed(paths, title, "1.9.0"));
    CHECK_FALSE(emu::coverMissed(paths, title, "1.10.0"));
    CHECK_FALSE(emu::coverMissed(paths, "Other", "1.9.0"));

    emu::clearCoverMisses(paths);
    CHECK_FALSE(emu::coverMissed(paths, title, "1.9.0"));
    CHECK_FALSE(fs::exists(emu::fromUtf8(emu::coverMissDir(paths))));

    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST_CASE("one cover miss can be cleared on its own") {
    fs::path dir = fs::temp_directory_path() /
                   ("equinox_covers_one_" +
                    std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    emu::ConsolePaths paths;
    paths.covers = emu::toUtf8(dir);

    REQUIRE(emu::markCoverMissed(paths, "A", "1.9.0"));
    REQUIRE(emu::markCoverMissed(paths, "B", "1.9.0"));
    emu::clearCoverMiss(paths, "A");
    CHECK_FALSE(emu::coverMissed(paths, "A", "1.9.0"));
    CHECK(emu::coverMissed(paths, "B", "1.9.0"));
    emu::clearCoverMiss(paths, "never marked");

    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST_CASE("png check and replace") {
    fs::path dir = fs::temp_directory_path() /
                   ("equinox_png_" +
                    std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(dir);
    const std::string png = emu::toUtf8(dir / "a.png.part");
    const std::string html = emu::toUtf8(dir / "b.part");
    {
        std::ofstream f(emu::fromUtf8(png), std::ios::binary);
        f << "\x89PNG\r\n\x1a\n" << "rest";
        std::ofstream g(emu::fromUtf8(html), std::ios::binary);
        g << "<html>";
    }
    CHECK(emu::looksLikePng(png));
    CHECK_FALSE(emu::looksLikePng(html));
    CHECK_FALSE(emu::looksLikePng(emu::toUtf8(dir / "missing")));

    const std::string target = emu::toUtf8(dir / "a.png");
    {
        std::ofstream old(emu::fromUtf8(target), std::ios::binary);
        old << "old";
    }
    REQUIRE(emu::replaceFile(png, target));
    CHECK_FALSE(fs::exists(emu::fromUtf8(png)));
    CHECK(emu::looksLikePng(target));

    std::error_code ec;
    fs::remove_all(dir, ec);
}
