#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <string>

// Self-update from the GitHub releases of this fork.
//
// Each release carries a Equinox.nro asset. On Switch, when the user
// asks from Settings, the app checks the latest release and, when it is newer,
// offers to download it. The download runs in the background as the
// "appupdate" DownloadService task; once it is checked and staged next to the
// running NRO, the app asks whether to restart now to install it.
namespace Updater {

struct UpdateInfo {
    std::string version;
    std::string url;
    size_t size = 0;
};

// Whether this build can replace itself: a Switch NRO launched from the SD card.
bool supported();

// True when `candidate` ("v1.5.5" or "1.5.5") is newer than `current`.
bool isNewer(const std::string& candidate, const std::string& current);

// Checks in the background and reports the result in a dialog. With `silent`,
// only an available update is shown. An update already downloading is only
// shown again; one already downloaded is offered without a new download.
void checkAndPrompt(bool silent);

// At start: offers the update a previous run downloaded, if any.
void offerStaged();

// The download is written to <target>.new.part and becomes <target>.new only
// once checked; <target>.new.version, written last, holds its version. A .new
// without a valid version file is not staged.
std::string stagedVersionPath(const std::string& target);

// A complete NRO: the "NRO0" magic and, unless 0, `expectedSize` bytes.
bool looksLikeNro(const std::string& path, size_t expectedSize);

enum class StageResult { Staged, MoveFailed, WriteFailed };

// Moves the checked <target>.new.part to <target>.new, then writes `version`
// beside it. Nothing is staged when either step fails.
StageResult stage(const std::string& target, const std::string& version);

// An install stopped between its two renames leaves only <target>.old: puts
// it back. True when it did.
bool restoreBackup(const std::string& target);

// At start, before anything reads the NRO: restoreBackup on the launch path.
void recoverInstall();

// The version staged for `target` when <target>.new is an NRO newer than
// `current`. Anything else staged (older, invalid, without its version) is
// removed.
std::optional<std::string> stagedUpdate(const std::string& target,
                                        const std::string& current);

// Removes <target>.new and its version file.
void clearStaged(const std::string& target);

} // namespace Updater
