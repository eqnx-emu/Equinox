#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "emu/Console.hpp"

namespace emu {

struct Manifest {
    std::string id, console, version, asset, sha256;
    int protocol = 0;
    uint64_t size = 0;
};

// nullopt when the JSON is invalid, a field is missing or has the wrong type.
std::optional<Manifest> parseManifest(const std::string& json);

// Compares dotted numeric versions: -1, 0 or 1. Missing parts count as 0.
int compareVersions(const std::string& a, const std::string& b);

struct Installed {
    std::string version, sha256;
    int protocol = 0;
    int64_t lastCheck = 0;
};

std::optional<Installed> readInstalled(const std::string& path);
// Writes to path + ".tmp" and renames over path.
bool writeInstalled(const std::string& path, const Installed& installed);

enum class RunnerAction { Install, Update, UpToDate, NeedsNewerEquinox };

RunnerAction decide(const std::optional<Installed>& installed, const Manifest& latest,
                    int supportedProtocol);

// True when a day has passed since lastCheck, or the clock went backwards.
bool shouldCheck(int64_t lastCheck, int64_t now);

// Streaming SHA-256 as lowercase hex; "" when the file cannot be read.
std::string sha256Hex(const std::string& filePath);
bool verifyFile(const std::string& filePath, const Manifest& manifest);

std::string manifestUrl(const Console& console);
std::string assetUrl(const Console& console, const Manifest& manifest);

}  // namespace emu
