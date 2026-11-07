#pragma once

#include <string>
#include <vector>

#include "emu/Console.hpp"

namespace emu {

// Global video defaults the runner reads through --video.
struct VideoDefaults {
    std::string size = "original", smooth = "pixel", filter = "none", border = "equinox";
};

// <emulator>/video.json
std::string videoDefaultsPath(const ConsolePaths& paths);
// An unknown value, a missing key or an unreadable file gives the default.
VideoDefaults readVideoDefaults(const std::string& path);
bool writeVideoDefaults(const std::string& path, const VideoDefaults& defaults);

// Menu order.
extern const std::vector<std::string> kSizeIds, kSmoothIds, kFilterIds, kBorderIds;

}  // namespace emu
