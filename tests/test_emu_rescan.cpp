#include "doctest/doctest.h"

#include "emu/Rescan.hpp"

using namespace emu;

TEST_CASE("only a rescan the user picked lets go of the covers they stopped") {
    CHECK(releasesCoverHolds(RescanFrom::Menu));
    CHECK(releasesCoverHolds(RescanFrom::EmptyShelf));
    CHECK_FALSE(releasesCoverHolds(RescanFrom::Shortcut));
    CHECK_FALSE(releasesCoverHolds(RescanFrom::Refresh));
}

TEST_CASE("the shortcut still tries missed covers again; a refresh does not") {
    CHECK(clearsCoverMisses(RescanFrom::Menu));
    CHECK(clearsCoverMisses(RescanFrom::EmptyShelf));
    CHECK(clearsCoverMisses(RescanFrom::Shortcut));
    CHECK_FALSE(clearsCoverMisses(RescanFrom::Refresh));
}
