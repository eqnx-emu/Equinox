#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>

#include "emu/CoverStore.hpp"
#include "emu/PathUtf8.hpp"
#include "emu/PlayedAgo.hpp"

namespace fs = std::filesystem;

static int64_t localTime(int year, int month, int day, int hour, int minute = 0) {
    std::tm tm{};
    tm.tm_year = year - 1900;
    tm.tm_mon = month - 1;
    tm.tm_mday = day;
    tm.tm_hour = hour;
    tm.tm_min = minute;
    tm.tm_isdst = -1;
    return static_cast<int64_t>(std::mktime(&tm));
}

TEST_CASE("daysSincePlayed counts calendar days") {
    const int64_t now = localTime(2026, 11, 7, 9, 30);
    CHECK(emu::daysSincePlayed(0, now) == -1);
    CHECK(emu::daysSincePlayed(-5, now) == -1);
    CHECK(emu::daysSincePlayed(localTime(2026, 11, 7, 0, 5), now) == 0);
    CHECK(emu::daysSincePlayed(now, now) == 0);
    // Late last night is yesterday, even if less than 24 hours ago.
    CHECK(emu::daysSincePlayed(localTime(2026, 11, 6, 23, 50), now) == 1);
    CHECK(emu::daysSincePlayed(localTime(2026, 11, 5, 12), now) == 2);
    // Across a month and a year.
    CHECK(emu::daysSincePlayed(localTime(2026, 10, 31, 12), localTime(2026, 11, 1, 8)) == 1);
    CHECK(emu::daysSincePlayed(localTime(2025, 12, 31, 20), localTime(2026, 1, 2, 8)) == 2);
    // A clock that went backwards: today.
    CHECK(emu::daysSincePlayed(now + 3 * 86400, now) == 0);
}

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
