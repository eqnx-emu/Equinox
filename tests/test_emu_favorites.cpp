#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include <jansson.h>

#include <cstdlib>
#include <string>

#include "emu/LocalFavorites.hpp"

static json_t* parse(const char* text) {
    json_error_t error;
    return json_loads(text, 0, &error);
}

TEST_CASE("an old settings.json without local_favorites loads empty") {
    json_t* root = parse(R"({"hosts": [], "settings": {"resolution": 720}})");
    REQUIRE(root);
    CHECK(emu::parseLocalFavorites(json_object_get(root, "local_favorites")).empty());
    CHECK(emu::parseLocalFavorites(nullptr).empty());
    json_decref(root);

    json_t* wrong = parse(R"({"local_favorites": {"console": "snes"}})");
    REQUIRE(wrong);
    CHECK(emu::parseLocalFavorites(json_object_get(wrong, "local_favorites")).empty());
    json_decref(wrong);
}

TEST_CASE("malformed entries are skipped, good ones kept in order") {
    json_t* root = parse(R"({"local_favorites": [
        {"console": "snes", "rom": "Super Mario World.sfc", "title": "Super Mario World"},
        42,
        "snes",
        {"console": "snes"},
        {"rom": "Lost.sfc"},
        {"console": 1, "rom": "Bad.sfc"},
        {"console": "snes", "rom": 7},
        {"console": "snes", "rom": "../escape.sfc"},
        {"console": "snes", "rom": "sub\\dir.sfc"},
        {"console": "snes", "rom": ".."},
        {"console": "snes", "rom": ""},
        {"console": "snes", "rom": "Super Mario World.sfc", "title": "Duplicate"},
        {"console": "snes", "rom": "F-Zero.smc"},
        {"console": "snes", "rom": "Chrono.zip", "title": 3}
    ]})");
    REQUIRE(root);
    const auto list = emu::parseLocalFavorites(json_object_get(root, "local_favorites"));
    json_decref(root);

    REQUIRE(list.size() == 3);
    CHECK(list[0].console == "snes");
    CHECK(list[0].romFile == "Super Mario World.sfc");
    CHECK(list[0].title == "Super Mario World");
    CHECK(list[1].romFile == "F-Zero.smc");
    CHECK(list[1].title == "F-Zero");
    CHECK(list[2].romFile == "Chrono.zip");
    CHECK(list[2].title == "Chrono");
}

TEST_CASE("toggle adds then removes, and the list round-trips through JSON") {
    std::vector<emu::LocalFavorite> list;
    const emu::LocalFavorite smw{"snes", "Super Mario World.sfc", "Super Mario World"};
    const emu::LocalFavorite zelda{"snes", "Zelda.sfc", "Zelda"};

    CHECK_FALSE(emu::isLocalFavorite(list, "snes", smw.romFile));
    CHECK(emu::toggleLocalFavorite(list, smw));
    CHECK(emu::toggleLocalFavorite(list, zelda));
    CHECK(emu::isLocalFavorite(list, "snes", smw.romFile));
    CHECK_FALSE(emu::isLocalFavorite(list, "nes", smw.romFile));

    json_t* array = emu::localFavoritesJson(list);
    REQUIRE(array);
    json_t* root = json_object();
    json_object_set_new(root, "local_favorites", array);
    char* text = json_dumps(root, 0);
    json_decref(root);
    REQUIRE(text);
    json_t* back = parse(text);
    free(text);
    REQUIRE(back);
    const auto copy = emu::parseLocalFavorites(json_object_get(back, "local_favorites"));
    json_decref(back);
    REQUIRE(copy.size() == 2);
    CHECK(copy[0].romFile == smw.romFile);
    CHECK(copy[0].title == smw.title);
    CHECK(copy[1].romFile == zelda.romFile);

    CHECK_FALSE(emu::toggleLocalFavorite(list, smw));
    REQUIRE(list.size() == 1);
    CHECK(list[0].romFile == zelda.romFile);

    // Nothing unsafe gets in through toggle either.
    CHECK_FALSE(emu::toggleLocalFavorite(list, {"snes", "../x.sfc", "x"}));
    CHECK_FALSE(emu::toggleLocalFavorite(list, {"", "x.sfc", "x"}));
    CHECK(list.size() == 1);
}
