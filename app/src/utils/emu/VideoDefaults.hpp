#pragma once

#include <string>
#include <vector>

#include "emu/Console.hpp"

namespace emu {

// Global video defaults the runner reads through --video.
struct VideoDefaults {
    std::string size = "original", smooth = "pixel", filter = "none", border = "equinox";
};

// What the console's runner uses when video.json has no value: PlayStation
// games fill a 4:3 TV whatever their resolution, so they start at 4:3.
VideoDefaults videoDefaultsFor(const Console& console);

// <emulator>/video.json
std::string videoDefaultsPath(const ConsolePaths& paths);
// When the file is missing but a .bak or .tmp of an interrupted write is
// there, puts it back at `path` (the runner only reads `path`).
void recoverVideoDefaults(const std::string& path);
// Recovers first. An unknown value, a missing key or an unreadable file gives
// the value in `base`.
VideoDefaults readVideoDefaults(const std::string& path, const VideoDefaults& base = {});
bool writeVideoDefaults(const std::string& path, const VideoDefaults& defaults);

// Menu order.
extern const std::vector<std::string> kSizeIds, kSmoothIds, kFilterIds, kBorderIds;

}  // namespace emu
