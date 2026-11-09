//
//  emu_video_view.hpp
//  Equinox
//
//  Video defaults of an emulator (size, smoothing, filter, border): the
//  values every game starts with. Each change is written to video.json
//  right away.
//

#pragma once

#include "emu/Console.hpp"
#include <borealis.hpp>

// Content for an AppletFrame; its title is already set.
brls::View* makeEmuVideoView(const emu::Console& console,
                             const emu::ConsolePaths& paths);
