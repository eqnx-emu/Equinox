#pragma once

#include <string>
#include <vector>

#include "emu/DownloadQueue.hpp"

namespace emu {

// Writes the GameData and Media tasks and the host blocks to `path`. Other
// kinds are cheap to rediscover and are not persisted.
bool saveDownloads(const std::string& path, const DownloadQueue& queue);

// Restores tasks through submit(): Running comes back Queued, Paused and Failed
// are kept. Falls back to `<path>.old` when `path` is missing. Returns false,
// leaving `queue` untouched, when nothing valid could be read.
bool loadDownloads(const std::string& path, DownloadQueue& queue);

// From a list of `.part` paths, those no queued task owns. A task owns the
// path stored under the "part" string key of its params JSON.
std::vector<std::string> orphanPartials(const std::vector<std::string>& found,
                                        const DownloadQueue& queue);

}  // namespace emu
