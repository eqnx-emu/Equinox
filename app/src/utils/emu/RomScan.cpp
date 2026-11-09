#include "emu/RomScan.hpp"
#include "emu/PathUtf8.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <system_error>

namespace fs = std::filesystem;

namespace emu {

static std::string lower(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

bool romNameMatches(const std::string& name, const Console& console) {
    if (name.empty() || name[0] == '.') return false;
    const size_t dot = name.rfind('.');
    if (dot == std::string::npos || dot == 0) return false;
    const std::string ext = lower(name.substr(dot));
    return std::find(console.extensions.begin(), console.extensions.end(), ext) !=
           console.extensions.end();
}

RomEntry romEntryFor(const std::string& dir, const std::string& name) {
    std::string path = dir;
    if (path.empty() || path.back() != '/') path += '/';
    path += name;
    return {path, name, name.substr(0, name.rfind('.'))};
}

bool romFileNameLess(const std::string& a, const std::string& b) {
    const std::string la = lower(a), lb = lower(b);
    if (la != lb) return la < lb;
    return a < b;
}

bool romFileUsable(const std::string& path) {
    std::error_code ec;
    const fs::path file = fromUtf8(path);
    if (!fs::is_regular_file(file, ec) || ec) return false;
    const std::uintmax_t size = fs::file_size(file, ec);
    return !ec && size > 0;
}

bool listRomNames(const std::string& dir, const Console& console, std::vector<std::string>& out) {
    try {
        std::error_code ec;
        fs::directory_iterator it(fromUtf8(dir), fs::directory_options::skip_permission_denied, ec);
        if (ec) return false;
        const fs::directory_iterator end;
        while (it != end) {
            std::string name = toUtf8(it->path().filename());
            if (romNameMatches(name, console)) out.push_back(std::move(name));
            it.increment(ec);
            if (ec) return false;
        }
        return true;
    } catch (...) {
        return false;
    }
}

std::vector<RomEntry> scanRoms(const std::string& dir, const Console& console) {
    std::vector<RomEntry> out;
    try {
        std::error_code ec;
        fs::path base = fromUtf8(dir);
        fs::directory_iterator it(base, fs::directory_options::skip_permission_denied, ec);
        if (ec) return out;
        const fs::directory_iterator end;
        while (it != end) {
            try {
                const fs::directory_entry& entry = *it;
                std::error_code e2;
                std::string name = toUtf8(entry.path().filename());
                if (romNameMatches(name, console) && entry.is_regular_file(e2) && !e2) {
                    std::uintmax_t size = entry.file_size(e2);
                    if (!e2 && size > 0) out.push_back(romEntryFor(dir, name));
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
        return romFileNameLess(a.fileName, b.fileName);
    });
    return out;
}

static fs::path romCountFile(const ConsolePaths& paths) {
    return fromUtf8(paths.emulator + "/rom-count.txt");
}

int readRomCount(const ConsolePaths& paths) {
    std::ifstream f(romCountFile(paths));
    int count = -1;
    if (!(f >> count) || count < 0) return -1;
    return count;
}

void writeRomCount(const ConsolePaths& paths, int count) {
    std::ofstream f(romCountFile(paths), std::ios::trunc);
    f << count;
}

}  // namespace emu
