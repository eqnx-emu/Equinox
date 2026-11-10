#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include <string>

#include "emu/SteamGridDb.hpp"

using namespace emu;

TEST_CASE("the search term drops the tags") {
    CHECK(sgdbSearchTerm("Super Metroid (Japan, USA) (En,Ja)", "x") == "Super Metroid");
    CHECK(sgdbSearchTerm("", "Chrono Trigger [!]") == "Chrono Trigger");
    CHECK(sgdbSearchUrl("Chrono Trigger").find("/search/autocomplete/Chrono%20Trigger") !=
          std::string::npos);
    CHECK(sgdbAuthHeader("abc") == "Authorization: Bearer abc");
}

TEST_CASE("search prefers the whole name, then a partial one") {
    const std::string body =
        R"({"success":true,"data":[{"id":7,"name":"Super Metroid: Redesign"},{"id":9,"name":"Super Metroid"}]})";
    SgdbGame g = sgdbParseSearch(200, body, "Super Metroid");
    CHECK(g.status == SgdbStatus::Ok);
    CHECK(g.id == 9);
    g = sgdbParseSearch(200, R"({"success":true,"data":[{"id":7,"name":"Super Metroid: Redesign"}]})",
                        "super metroid");
    CHECK(g.id == 7);
    g = sgdbParseSearch(200, R"({"success":true,"data":[{"id":3,"name":"Zelda"}]})", "Metroid");
    CHECK(g.status == SgdbStatus::NotFound);
    CHECK(sgdbParseSearch(200, R"({"success":true,"data":[]})", "x").status == SgdbStatus::NotFound);
}

TEST_CASE("statuses") {
    CHECK(sgdbParseSearch(401, "", "x").status == SgdbStatus::BadKey);
    CHECK(sgdbParseSearch(403, "", "x").status == SgdbStatus::BadKey);
    CHECK(sgdbParseSearch(404, "", "x").status == SgdbStatus::NotFound);
    CHECK(sgdbParseSearch(429, "", "x").status == SgdbStatus::Busy);
    CHECK(sgdbParseSearch(0, "", "x").status == SgdbStatus::Busy);
    CHECK(sgdbParseGrids(500, "").status == SgdbStatus::Busy);
    CHECK(sgdbParseSearch(200, "<html>", "x").status == SgdbStatus::Busy);
}

TEST_CASE("grids: a wide image first, else a tall one, never other formats") {
    std::string body = R"({"success":true,"data":[
        {"mime":"image/webp","url":"https://cdn/a.webp","width":920,"height":430},
        {"mime":"image/png","url":"https://cdn/tall.png","width":600,"height":900},
        {"mime":"image/jpeg","url":"https://cdn/wide.jpg","width":920,"height":430}]})";
    SgdbGrid g = sgdbParseGrids(200, body);
    CHECK(g.status == SgdbStatus::Ok);
    CHECK(g.url == "https://cdn/wide.jpg");
    body = R"({"success":true,"data":[{"mime":"image/png","url":"https://cdn/tall.png","width":600,"height":900}]})";
    CHECK(sgdbParseGrids(200, body).url == "https://cdn/tall.png");
    body = R"({"success":true,"data":[{"mime":"image/png","url":"http://cdn/plain.png","width":920,"height":430}]})";
    CHECK(sgdbParseGrids(200, body).status == SgdbStatus::NotFound);
}
