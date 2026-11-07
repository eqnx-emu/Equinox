#include "emu/CoverName.hpp"

#include <algorithm>

namespace emu {

static std::string tidy(const std::string& s) {
    std::string r;
    for (char c : s) {
        if (c == ' ' && (r.empty() || r.back() == ' ')) continue;
        r += c;
    }
    while (!r.empty() && r.back() == ' ') r.pop_back();
    return r;
}

static std::string stripBlocks(const std::string& s, char open, char close) {
    std::string r;
    size_t i = 0;
    while (i < s.size()) {
        if (s[i] == open) {
            size_t j = s.find(close, i + 1);
            if (j != std::string::npos) {
                i = j + 1;
                continue;
            }
        }
        r += s[i++];
    }
    return r;
}

static std::string replaceAll(std::string s, const std::string& from, const std::string& to) {
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
    return s;
}

std::vector<std::string> coverNameCandidates(const std::string& title) {
    std::string noBrackets = stripBlocks(title, '[', ']');
    std::string c3 = tidy(noBrackets);
    c3 = replaceAll(c3, "(U)", "(USA)");
    c3 = replaceAll(c3, "(E)", "(Europe)");
    c3 = replaceAll(c3, "(J)", "(Japan)");
    const std::string cands[] = {tidy(title), tidy(noBrackets), c3,
                                 tidy(stripBlocks(noBrackets, '(', ')'))};

    static const std::string bad = "&*/:`<>?\\|\"";
    std::vector<std::string> out;
    for (std::string c : cands) {
        if (c.empty()) continue;
        for (char& ch : c)
            if (bad.find(ch) != std::string::npos) ch = '_';
        if (std::find(out.begin(), out.end(), c) == out.end()) out.push_back(c);
    }
    return out;
}

static std::string urlEncode(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string r;
    for (unsigned char c : s) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~') {
            r += static_cast<char>(c);
        } else {
            r += '%';
            r += hex[c >> 4];
            r += hex[c & 15];
        }
    }
    return r;
}

std::string coverUrl(const Console& console, const std::string& candidate) {
    return "https://thumbnails.libretro.com/" + urlEncode(console.libretroSystem) +
           "/Named_Boxarts/" + urlEncode(candidate) + ".png";
}

std::string coverPath(const ConsolePaths& paths, const std::string& title) {
    return paths.covers + "/" + title + ".png";
}

}  // namespace emu
