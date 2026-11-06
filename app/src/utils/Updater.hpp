#pragma once

#include <cstddef>
#include <functional>
#include <string>

// Self-update from the GitHub releases of this fork.
//
// Each release carries a Equinox.nro asset. On Switch, when the user
// asks from Settings, the app checks the latest release and, when it is newer,
// offers to download it, replace the NRO it was launched from and relaunch.
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
// only an available update is shown.
void checkAndPrompt(bool silent);

} // namespace Updater
