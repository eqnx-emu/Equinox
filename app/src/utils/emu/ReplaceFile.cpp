#include "emu/ReplaceFile.hpp"

#include <system_error>

namespace emu {

namespace fs = std::filesystem;

bool replaceKeepingBackup(const fs::path& from, const fs::path& to, const fs::path& backup) {
    std::error_code ec;
    fs::remove(backup, ec);
    if (fs::exists(backup, ec)) return false;

    const bool hadOld = fs::exists(to, ec);
    if (hadOld) {
        fs::rename(to, backup, ec);
        if (ec) return false;
    }

    fs::rename(from, to, ec);
    if (ec) {
        if (hadOld) {
            std::error_code restoreEc;
            fs::rename(backup, to, restoreEc);
        }
        return false;
    }

    if (hadOld) fs::remove(backup, ec);
    return true;
}

}  // namespace emu
