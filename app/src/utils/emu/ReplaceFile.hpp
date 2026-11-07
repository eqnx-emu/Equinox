#pragma once

#include <filesystem>

namespace emu {

// Moves `from` onto `to` keeping the old file until the new one is in place.
// Horizon's FAT driver will not rename over an existing file, so every rename
// here targets a name that does not exist: the old file goes to `backup`
// first and comes back if the new one cannot be moved in. If the process dies
// between the two renames, `backup` (and `from`) are left for recovery.
bool replaceKeepingBackup(const std::filesystem::path& from, const std::filesystem::path& to,
                          const std::filesystem::path& backup);

}  // namespace emu
