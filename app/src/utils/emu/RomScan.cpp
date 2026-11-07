#include "emu/RomScan.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <system_error>

namespace fs = std::filesystem;

namespace emu {

static std::string lower(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

std::vector<RomEntry> scanRoms(const std::string& dir, const Console& console) {
    std::vector<RomEntry> out;
    try {
        std::error_code ec;
        fs::path base = fs::u8path(dir);
        fs::directory_iterator it(base, fs::directory_options::skip_permission_denied, ec);
        if (ec) return out;
        const fs::directory_iterator end;
        while (it != end) {
            try {
                const fs::directory_entry& entry = *it;
                std::error_code e2;
                std::string name = entry.path().filename().u8string();
                if (!name.empty() && name[0] != '.' && entry.is_regular_file(e2) && !e2) {
                    std::uintmax_t size = entry.file_size(e2);
                    size_t dot = name.rfind('.');
                    if (!e2 && size > 0 && dot != std::string::npos && dot > 0) {
                        std::string ext = lower(name.substr(dot));
                        if (std::find(console.extensions.begin(), console.extensions.end(), ext) !=
                            console.extensions.end()) {
                            std::string path = dir;
                            if (path.empty() || path.back() != '/') path += '/';
                            path += name;
                            out.push_back({path, name, name.substr(0, dot)});
                        }
                    }
                }
            } catch (...) {
            }
            std::error_code e3;
            it.increment(e3);
            if (e3) break;
        }
    } catch (...) {
    }
    std::sort(out.begin(), out.end(), [](const RomEntry& a, const RomEntry& b) {
        std::string la = lower(a.fileName), lb = lower(b.fileName);
        if (la != lb) return la < lb;
        return a.fileName < b.fileName;
    });
    return out;
}

}  // namespace emu
