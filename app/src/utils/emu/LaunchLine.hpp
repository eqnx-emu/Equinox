#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "emu/Console.hpp"
#include "emu/RomScan.hpp"

namespace emu {

// Same rule as the runner: drops '"', bytes < 0x20 and 0x7f.
std::string sanitizeArg(std::string_view value);

// A launch path is usable only as sdmc:/...*.nro; otherwise the Equinox default.
std::string returnPath(const std::string& launchPath);

enum class Origin { Shelf, Favorites };

struct ResumeToken {
    std::string console;
    Origin origin = Origin::Shelf;
    int filter = -1;  // favorites chip index, -1 = all
    std::string romFile;
};

// "1|snes|shelf|-1|<romFile>"
std::string encodeToken(const ResumeToken& token);
// nullopt when the version is not 1 or the token is malformed.
std::optional<ResumeToken> decodeToken(const std::string& text);

std::string runnerArgs(const Console& console, const ConsolePaths& paths, const RomEntry& rom,
                       const std::string& returnTo, const std::string& token);

// Value after "--resume": tokens up to the next "--*" joined by single spaces.
std::optional<std::string> resumeArg(int argc, const char* const* argv);

}  // namespace emu
