#include "emu/RomScan.hpp"
#include "emu/PathUtf8.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <set>
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
    const std::string base = romBaseName(name);
    return {path, name, base.substr(0, base.rfind('.'))};
}

bool romFileNameLess(const std::string& a, const std::string& b) {
    const std::string ba = lower(romBaseName(a)), bb = lower(romBaseName(b));
    if (ba != bb) return ba < bb;
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

namespace {

bool hasExtension(const std::string& name, const std::string& ext) {
    const std::string l = lower(name);
    return l.size() > ext.size() && l.compare(l.size() - ext.size(), ext.size(), ext) == 0;
}

// The discs an .m3u lists, as written in it, lower case.
void addListedDiscs(const fs::path& m3u, std::set<std::string>& listed) {
    std::ifstream f(m3u);
    std::string line;
    while (std::getline(f, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t'))
            line.pop_back();
        size_t start = 0;
        while (start < line.size() && (line[start] == ' ' || line[start] == '\t')) start++;
        line.erase(0, start);
        if (line.empty() || line[0] == '#') continue;
        for (char& c : line)
            if (c == '\\') c = '/';
        if (line.rfind("./", 0) == 0) line.erase(0, 2);
        listed.insert(lower(line));
    }
}

// Walks `dir` and the folders below it, adding each game as its path under the
// console's folder. `checkFiles` asks the card that each match is a non-empty
// file; without it only the names are read. False when a folder could not be
// read to the end.
bool walkRoms(const fs::path& dir, const std::string& prefix, int depth, const Console& console,
              bool checkFiles, std::vector<std::string>& out) {
    std::error_code ec;
    fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec);
    if (ec) return false;
    const fs::directory_iterator end;
    std::vector<std::string> games, folders;
    bool whole = true;
    while (it != end) {
        try {
            const fs::directory_entry& entry = *it;
            std::string name = toUtf8(entry.path().filename());
            std::error_code e2;
            if (romNameMatches(name, console)) {
                bool usable = true;
                if (checkFiles) {
                    usable = entry.is_regular_file(e2) && !e2;
                    if (usable) usable = entry.file_size(e2) > 0 && !e2;
                }
                if (usable) games.push_back(std::move(name));
            } else if (depth < kRomFolderDepth && !name.empty() && name[0] != '.' &&
                       entry.is_directory(e2) && !e2) {
                folders.push_back(std::move(name));
            }
        } catch (...) {
        }
        it.increment(ec);
        if (ec) {
            whole = false;
            break;
        }
    }

    // A multi-disc game shows once, as its .m3u, not once per disc.
    std::set<std::string> listed;
    for (const std::string& name : games)
        if (hasExtension(name, ".m3u")) addListedDiscs(dir / fromUtf8(name), listed);
    for (const std::string& name : games)
        if (hasExtension(name, ".m3u") || !listed.count(lower(name)))
            out.push_back(prefix + name);

    for (const std::string& folder : folders)
        if (!walkRoms(dir / fromUtf8(folder), prefix + folder + "/", depth + 1, console,
                      checkFiles, out))
            whole = false;
    return whole;
}

}  // namespace

bool listRomNames(const std::string& dir, const Console& console, std::vector<std::string>& out) {
    try {
        return walkRoms(fromUtf8(dir), "", 0, console, false, out);
    } catch (...) {
        return false;
    }
}

std::vector<RomEntry> scanRoms(const std::string& dir, const Console& console) {
    std::vector<std::string> names;
    try {
        walkRoms(fromUtf8(dir), "", 0, console, true, names);
    } catch (...) {
    }
    std::vector<RomEntry> out;
    out.reserve(names.size());
    for (const std::string& name : names) out.push_back(romEntryFor(dir, name));
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
