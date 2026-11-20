#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include "FocusRule.hpp"

TEST_CASE("a screen under a dialog leaves the focus to the dialog") {
    CHECK(screenFocusMove(false, false, false) == FocusMove::Keep);
    CHECK(screenFocusMove(false, true, false) == FocusMove::Keep);
}

TEST_CASE("a screen on top keeps a focus that is inside it and shown") {
    CHECK(screenFocusMove(true, true, true) == FocusMove::Keep);
}

TEST_CASE("a screen on top takes back a focus left outside it") {
    // The ghost: a view of the screen below, or one in the frame's header.
    CHECK(screenFocusMove(true, false, false) == FocusMove::ToScreen);
    CHECK(screenFocusMove(true, false, true) == FocusMove::ToScreen);
}

TEST_CASE("a screen on top moves a focus inside it that is no longer shown") {
    CHECK(screenFocusMove(true, true, false) == FocusMove::ToScreen);
}
