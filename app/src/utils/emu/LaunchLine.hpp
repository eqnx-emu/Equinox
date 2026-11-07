#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "emu/Console.hpp"
#include "emu/RomScan.hpp"

namespace emu {

// Same rule as the runner: drops '"', bytes < 0x20 and 0x7f.
std::string sanitizeArg(std::string_view value);

// A launch path is usable only as sdmc:/...*.nro; otherwise the Equinox default.
std::string returnPath(const std::string& launchPath);

// "/switch/x.nro" (argv[0] without a device) becomes "sdmc:/switch/x.nro";
// anything else is returned as is.
std::string normalizeLaunchPath(const std::string& launchPath);

// Where Equinox may be, best first, without repeats: the path it was started
// from (when usable), then the README's sdmc:/switch/Equinox/Equinox.nro,
// then sdmc:/switch/Equinox.nro.
std::vector<std::string> returnCandidates(const std::string& launchPath);

// The first candidate for which `exists` is true; nullopt when none is.
std::optional<std::string> pickReturnPath(const std::string& launchPath,
                                          const std::function<bool(const std::string&)>& exists);

enum class Origin { Shelf, Favorites };

struct ResumeToken {
    std::string console;
    Origin origin = Origin::Shelf;
    // Origin::Favorites: position of the selected chip in the Favorites
    // chip row as it was built at launch (0 = "All", then one chip per PC,
    // then one per console; the L/R hints do not count). FavoritesView
    // rebuilds the row on return and uses "All" unless that position is
    // still the chip of this token's console. -1 (the shelf) also means all.
    int filter = -1;
    std::string romFile;
};

// "1|snes|shelf|-1|<romFile>"
std::string encodeToken(const ResumeToken& token);
// nullopt when the version is not 1 or the token is malformed.
std::optional<ResumeToken> decodeToken(const std::string& text);

std::string runnerArgs(const Console& console, const ConsolePaths& paths, const RomEntry& rom,
                       const std::string& returnTo, const std::string& token,
                       const std::string& cover = "", const std::string& video = "");

// Value after "--resume": tokens up to the next "--*" joined by single spaces.
std::optional<std::string> resumeArg(int argc, const char* const* argv);

}  // namespace emu
