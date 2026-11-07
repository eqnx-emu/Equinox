#include "emu/GameDb.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace emu {

namespace {

std::vector<std::string> splitTabs(const std::string& line) {
    std::vector<std::string> out;
    size_t start = 0;
    for (;;) {
        size_t tab = line.find('\t', start);
        if (tab == std::string::npos) {
            out.push_back(line.substr(start));
            return out;
        }
        out.push_back(line.substr(start, tab - start));
        start = tab + 1;
    }
}

bool parseCrc(const std::string& s, uint32_t& out) {
    if (s.size() != 8) return false;
    uint32_t v = 0;
    for (char c : s) {
        int d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else return false;
        v = (v << 4) | static_cast<uint32_t>(d);
    }
    out = v;
    return true;
}

int parseInt(const std::string& s) {
    if (s.empty()) return 0;
    for (char c : s)
        if (c < '0' || c > '9') return 0;
    return std::atoi(s.c_str());
}

}  // namespace

bool GameDb::loadText(const std::string& text) {
    games_.clear();
    std::istringstream in(text);
    std::string line;
    bool first = true;
    while (std::getline(in, line)) {
        // git autocrlf can check the file out with CRLF.
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (first) {
            first = false;
            auto h = splitTabs(line);
            if (h.size() < 2 || h[0] != "#equinox-gamedb" || h[1] != "1") return false;
            continue;
        }
        auto f = splitTabs(line);
        GameInfo g;
        if (f.size() < 8 || !parseCrc(f[0], g.crc) || f[1].empty()) continue;
        g.name = f[1];
        g.genre = f[2];
        g.players = parseInt(f[3]);
        g.developer = f[4];
        g.publisher = f[5];
        g.year = parseInt(f[6]);
        g.franchise = f[7];
        games_.push_back(std::move(g));
    }
    if (first) return false;
    std::stable_sort(games_.begin(), games_.end(),
                     [](const GameInfo& a, const GameInfo& b) { return a.crc < b.crc; });
    return true;
}

bool GameDb::loadFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        games_.clear();
        return false;
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return loadText(ss.str());
}

const GameInfo* GameDb::find(uint32_t crc) const {
    auto it = std::lower_bound(games_.begin(), games_.end(), crc,
                               [](const GameInfo& g, uint32_t c) { return g.crc < c; });
    return it != games_.end() && it->crc == crc ? &*it : nullptr;
}

std::string gameDbPath(const Console& console) {
#ifdef BRLS_RESOURCES
    return std::string(BRLS_RESOURCES) + "gamedb/" + console.slug + ".tsv";
#else
    (void)console;
    return {};
#endif
}

}  // namespace emu
