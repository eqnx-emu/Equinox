#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include <string>

#include "emu/GameDb.hpp"

using emu::GameDb;

namespace {
const std::string kHeader = "#equinox-gamedb\t1\tabc123\n";
}

TEST_CASE("loads rows, empty fields and non numeric players") {
    GameDb db;
    std::string text = kHeader +
        "003764e5\tDerby Stallion II (Japan)\t\t\t\t\t\t\n"
        "b19ed489\tSuper Mario World (USA)\tPlatform\t2\tNintendo EAD\tNintendo\t1991\tSuper Mario Bros\n"
        "c0ffee00\tOdd Game\tPuzzle\tmany\tDev\tPub\t1995\t\n"
        "deadbeef\tShort\tRow\n";
    CHECK(db.loadText(text));
    CHECK(db.size() == 3);

    const auto* smw = db.find(0xb19ed489);
    REQUIRE(smw);
    CHECK(smw->name == "Super Mario World (USA)");
    CHECK(smw->genre == "Platform");
    CHECK(smw->players == 2);
    CHECK(smw->developer == "Nintendo EAD");
    CHECK(smw->publisher == "Nintendo");
    CHECK(smw->year == 1991);
    CHECK(smw->franchise == "Super Mario Bros");

    const auto* derby = db.find(0x003764e5);
    REQUIRE(derby);
    CHECK(derby->genre.empty());
    CHECK(derby->players == 0);
    CHECK(derby->year == 0);

    const auto* odd = db.find(0xc0ffee00);
    REQUIRE(odd);
    CHECK(odd->players == 0);
    CHECK(odd->year == 1995);

    CHECK(db.find(0xdeadbeef) == nullptr);
    CHECK(db.find(0x12345678) == nullptr);
}

TEST_CASE("wrong header is rejected") {
    GameDb db;
    CHECK_FALSE(db.loadText("#equinox-gamedb\t2\tx\nb19ed489\tSuper Mario World (USA)\n"));
    CHECK(db.size() == 0);
    CHECK_FALSE(db.loadText("b19ed489\tSuper Mario World (USA)\n"));
    CHECK(db.size() == 0);
}

TEST_CASE("CRLF line endings parse the same") {
    GameDb db;
    CHECK(db.loadText("#equinox-gamedb\t1\tabc\r\nb19ed489\tSuper Mario World (USA)\tPlatform\t2\tA\tB\t1991\tFranchise\r\n"));
    const auto* g = db.find(0xb19ed489);
    REQUIRE(g);
    CHECK(g->franchise == "Franchise");
    CHECK(g->name == "Super Mario World (USA)");
}

TEST_CASE("bundled snes database finds Super Mario World") {
    GameDb db;
    REQUIRE(db.loadFile(std::string(MOONLIGHT_ROOT) + "/resources/gamedb/snes.tsv"));
    CHECK(db.size() > 1000);
    const auto* g = db.find(0xb19ed489);
    REQUIRE(g);
    CHECK(g->name == "Super Mario World (USA)");
    CHECK(g->year == 1991);
}

TEST_CASE("missing file fails") {
    GameDb db;
    CHECK_FALSE(db.loadFile("does/not/exist.tsv"));
    CHECK(db.size() == 0);
}
