#pragma once

#include <optional>
#include <string>

#include "HttpDownload.hpp"
#include "emu/Console.hpp"
#include "emu/RunnerManifest.hpp"

namespace emu {

// What went wrong, for a translated message; `error` keeps the detail.
enum class RunnerError {
    None,
    Network,         // a download failed (curl's text in `error`)
    Disk,            // runner.bin.part could not be written (full card, read-only)
    BadManifest,     // the manifest is not valid JSON or misses fields
    WrongConsole,    // the manifest names another console
    Folders,         // the folders could not be created
    Mismatch,        // the runner's size or SHA-256 differs from the manifest
    Move,            // runner.bin could not be put in place
    WriteInstalled,  // installed.json could not be written
    Cancelled,
};

struct InstallResult {
    bool ok = false;
    std::string error;
    Manifest manifest;
    bool cancelled = false;  // the Progress callback returned false
    RunnerError code = RunnerError::None;
    long httpStatus = 0;  // Network: the status the server answered with, 0 for none
};

// Downloads and parses the console's latest manifest. nullopt with `error`
// set when the download fails, the JSON is invalid or it names another console.
// `code`, when given, receives the kind of failure.
std::optional<Manifest> fetchLatest(const Console& console, std::string& error,
                                    RunnerError* code = nullptr);

// Downloads the runner to <emulator>/runner.bin.part, continuing an earlier
// attempt of the same file, checks size and SHA-256, swaps it in for
// runner.bin (the old one is kept as runner.bin.old until the new one is in
// place) and records it in installed.json. A network failure, a full card or a
// stop (the Progress callback returned false) keeps the .part for the next
// attempt; a file that fails the check is removed. The previous runner.bin is
// left as it was on any failure.
InstallResult installRunner(const Console& console, const Manifest& manifest,
                            const Progress& progress,
                            const std::string& root = "sdmc:/equinox");

// Removes what a stopped installRunner kept: after a user cancel.
void discardRunnerDownload(const Console& console, const std::string& root = "sdmc:/equinox");

// Deletes only <emulator>; ROMs, saves, states and covers stay.
bool removeRunner(const Console& console, const std::string& root = "sdmc:/equinox");

}  // namespace emu
