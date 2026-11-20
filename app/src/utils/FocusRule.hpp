#pragma once

// What a full screen does with the app's focus on each frame.
enum class FocusMove { Keep, ToScreen };

// screenOnTop: no dialog or other activity is over the screen.
// focusInScreen: the focused view is one of the screen's own.
// focusShown: it, and every parent up to the screen, is visible.
// A focus left outside the screen (on the screen below it, or in the frame's
// header) is drawn where nothing is and takes no B: the screen claims it.
inline FocusMove screenFocusMove(bool screenOnTop, bool focusInScreen, bool focusShown) {
    if (!screenOnTop)
        return FocusMove::Keep;
    if (focusInScreen && focusShown)
        return FocusMove::Keep;
    return FocusMove::ToScreen;
}
