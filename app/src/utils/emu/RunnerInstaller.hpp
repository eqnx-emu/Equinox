#pragma once

#include <optional>
#include <string>

#include "HttpDownload.hpp"
#include "emu/Console.hpp"
#include "emu/RunnerManifest.hpp"

namespace emu {

struct InstallResult {
    bool ok = false;
    std::string error;
    Manifest manifest;
};

// Downloads and parses the console's latest manifest. nullopt with `error`
// set when the download fails, the JSON is invalid or it names another console.
std::optional<Manifest> fetchLatest(const Console& console, std::string& error);

// Downloads the runner to <emulator>/runner.bin.part, checks size and SHA-256,
// moves it over runner.bin and records it in installed.json. On failure the
// .part file is removed and the previous runner.bin is left as it was.
InstallResult installRunner(const Console& console, const Manifest& manifest,
                            const Progress& progress,
                            const std::string& root = "sdmc:/equinox");

// Deletes only <emulator>; ROMs, saves, states and covers stay.
bool removeRunner(const Console& console, const std::string& root = "sdmc:/equinox");

}  // namespace emu
