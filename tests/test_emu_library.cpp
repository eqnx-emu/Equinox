#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include <string>
#include <vector>

#include "emu/Library.hpp"

using namespace emu;

TEST_CASE("displayName strips tags and moves the article") {
    CHECK(displayName("Legend of Zelda, The - A Link to the Past (USA)") == "The Legend of Zelda - A Link to the Past");
    CHECK(displayName("Super Mario World (U) [!]") == "Super Mario World");
    CHECK(displayName("Chrono Trigger") == "Chrono Trigger");
    CHECK(displayName("Legend of Zelda, The") == "The Legend of Zelda");
}

TEST_CASE("foldForSearch lowers case and drops accents") {
    CHECK(foldForSearch("Pokémon ÉLITE") == "pokemon elite");
    CHECK(foldForSearch("Ação") == "acao");
}

namespace {
std::vector<LibraryItem> games() {
    return {{"Super Mario World", 1991, "Plataforma"},
            {"The Legend of Zelda - A Link to the Past", 1992, "Ação e aventura"},
            {"Super Metroid", 1994, "Ação e aventura"},
            {"Chrono Trigger", 1995, "RPG"},
            {"Donkey Kong Country", 1994, "Plataforma"},
            {"Street Fighter II Turbo", 1993, "Luta"},
            {"Zzz Hack", 0, ""}};
}

std::vector<std::string> names(const std::vector<LibraryItem>& items, const std::vector<size_t>& order) {
    std::vector<std::string> out;
    for (size_t i : order) out.push_back(items[i].name);
    return out;
}
}  // namespace

TEST_CASE("libraryOrder sorts by name ignoring The") {
    auto g = games();
    CHECK(names(g, libraryOrder(g, "", LibrarySort::NameAsc)) ==
          std::vector<std::string>{"Chrono Trigger", "Donkey Kong Country", "The Legend of Zelda - A Link to the Past",
                                   "Street Fighter II Turbo", "Super Mario World", "Super Metroid", "Zzz Hack"});
    auto desc = names(g, libraryOrder(g, "", LibrarySort::NameDesc));
    CHECK(desc.front() == "Zzz Hack");
    CHECK(desc.back() == "Chrono Trigger");
}

TEST_CASE("libraryOrder sorts by year with unknown last") {
    auto g = games();
    auto asc = names(g, libraryOrder(g, "", LibrarySort::YearAsc));
    CHECK(asc.front() == "Super Mario World");
    CHECK(asc.back() == "Zzz Hack");
    auto desc = names(g, libraryOrder(g, "", LibrarySort::YearDesc));
    CHECK(desc.front() == "Chrono Trigger");
    CHECK(desc.back() == "Zzz Hack");
    // 1994 tie broken by name
    CHECK(desc[1] == "Donkey Kong Country");
    CHECK(desc[2] == "Super Metroid");
}

TEST_CASE("libraryOrder groups by genre with empty last") {
    auto g = games();
    auto o = names(g, libraryOrder(g, "", LibrarySort::Genre));
    CHECK(o.back() == "Zzz Hack");
    CHECK(o[0] == "The Legend of Zelda - A Link to the Past");
    CHECK(o[1] == "Super Metroid");
    CHECK(o[2] == "Street Fighter II Turbo");
}

TEST_CASE("libraryOrder filters by accent-insensitive query") {
    std::vector<LibraryItem> g = {{"Pokémon Gold", 1999, "RPG"}, {"Super Mario World", 1991, ""}, {"Super Metroid", 1994, ""}};
    CHECK(names(g, libraryOrder(g, "pokemon", LibrarySort::NameAsc)) == std::vector<std::string>{"Pokémon Gold"});
    CHECK(libraryOrder(g, "SUPER", LibrarySort::NameAsc).size() == 2);
    CHECK(libraryOrder(g, "nada", LibrarySort::NameAsc).empty());
    CHECK(libraryOrder(g, "  ", LibrarySort::NameAsc).size() == 3);
    CHECK(libraryOrder(g, " super ", LibrarySort::NameAsc).size() == 2);
}

TEST_CASE("noIntroRegions reads the first group") {
    CHECK(noIntroRegions("Super Mario World (USA)") == std::vector<std::string>{"USA"});
    CHECK(noIntroRegions("Super Metroid (Japan, USA) (En,Ja)") == std::vector<std::string>{"Japan", "USA"});
    CHECK(noIntroRegions("Chrono Trigger").empty());
    CHECK(noIntroRegions("X (En,Fr,De)").empty());
    CHECK(noIntroRegions("X (Rev 1)").empty());
}
