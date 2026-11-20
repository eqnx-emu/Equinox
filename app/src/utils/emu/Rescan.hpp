#pragma once

namespace emu {

// Who asked a console shelf to read its games again.
enum class RescanFrom {
    Refresh,     // the shelf itself, after a missing or unusable game
    Shortcut,    // the hidden ZL shortcut
    Menu,        // "Search again" picked in the options
    EmptyShelf,  // Y on the empty shelf
};

// Covers libretro lacked are tried again whenever the user asks.
inline bool clearsCoverMisses(RescanFrom from) { return from != RescanFrom::Refresh; }

// Covers the user stopped in Downloads come back only through a rescan they
// picked on screen, never through a shortcut pressed by chance.
inline bool releasesCoverHolds(RescanFrom from) {
    return from == RescanFrom::Menu || from == RescanFrom::EmptyShelf;
}

}  // namespace emu
