#include "emu/LaunchLine.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstring>

namespace emu {

namespace {

const char* const kDefaultReturn = "sdmc:/switch/Equinox.nro";

std::string quoted(std::string_view value) { return "\"" + sanitizeArg(value) + "\""; }

}  // namespace

std::string sanitizeArg(std::string_view value) {
    std::string out;
    out.reserve(value.size());
    for (char c : value) {
        unsigned char u = static_cast<unsigned char>(c);
        if (c == '"' || u < 0x20 || u == 0x7f) continue;
        out.push_back(c);
    }
    return out;
}

std::string normalizeLaunchPath(const std::string& launchPath) {
    if (!launchPath.empty() && launchPath[0] == '/') return "sdmc:" + launchPath;
    return launchPath;
}

std::string returnPath(const std::string& rawPath) {
    const std::string launchPath = normalizeLaunchPath(rawPath);
    static const std::string prefix = "sdmc:/";
    static const std::string suffix = ".nro";
    if (launchPath.size() < prefix.size() + suffix.size() ||
        launchPath.compare(0, prefix.size(), prefix) != 0)
        return kDefaultReturn;
    for (size_t i = 0; i < suffix.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(launchPath[launchPath.size() - suffix.size() + i]);
        if (std::tolower(c) != suffix[i]) return kDefaultReturn;
    }
    return launchPath;
}

std::vector<std::string> returnCandidates(const std::string& launchPath) {
    std::vector<std::string> out;
    auto add = [&out](const std::string& path) {
        if (std::find(out.begin(), out.end(), path) == out.end()) out.push_back(path);
    };
    const std::string own = returnPath(launchPath);
    // returnPath falls back to the default; only a real path of its own
    // goes first.
    if (own == normalizeLaunchPath(launchPath)) add(own);
    add("sdmc:/switch/Equinox/Equinox.nro");
    add(kDefaultReturn);
    return out;
}

std::optional<std::string> pickReturnPath(const std::string& launchPath,
                                          const std::function<bool(const std::string&)>& exists) {
    for (const std::string& path : returnCandidates(launchPath))
        if (exists(path)) return path;
    return std::nullopt;
}

std::string encodeToken(const ResumeToken& token) {
    return "1|" + token.console + "|" + (token.origin == Origin::Favorites ? "favorites" : "shelf") + "|" +
           std::to_string(token.filter) + "|" + token.romFile;
}

std::optional<ResumeToken> decodeToken(const std::string& text) {
    std::string parts[5];
    size_t start = 0;
    for (int i = 0; i < 4; ++i) {
        size_t bar = text.find('|', start);
        if (bar == std::string::npos) return std::nullopt;
        parts[i] = text.substr(start, bar - start);
        start = bar + 1;
    }
    parts[4] = text.substr(start);
    if (parts[4].find('|') != std::string::npos) return std::nullopt;
    if (parts[0] != "1" || parts[1].empty() || parts[4].empty()) return std::nullopt;

    ResumeToken token;
    token.console = parts[1];
    if (parts[2] == "shelf") token.origin = Origin::Shelf;
    else if (parts[2] == "favorites") token.origin = Origin::Favorites;
    else return std::nullopt;

    int filter = 0;
    const char* b = parts[3].data();
    const char* e = b + parts[3].size();
    auto res = std::from_chars(b, e, filter);
    if (b == e || res.ec != std::errc() || res.ptr != e || filter < -1) return std::nullopt;
    token.filter = filter;
    token.romFile = parts[4];
    return token;
}

std::string runnerArgs(const Console& console, const ConsolePaths& paths, const RomEntry& rom,
                       const std::string& returnTo, const std::string& token,
                       const std::string& cover, const std::string& video) {
    (void)console;
    std::string line = "--protocol 1 --rom " + quoted(paths.roms + "/" + rom.fileName) + " --title " + quoted(rom.title) +
           " --saves " + quoted(paths.saves) + " --states " + quoted(paths.states) + " --return " +
           quoted(returnTo) + " --resume " + quoted(token);
    if (!cover.empty()) line += " --cover " + quoted(cover);
    if (!video.empty()) line += " --video " + quoted(video);
    return line;
}

std::optional<std::string> resumeArg(int argc, const char* const* argv) {
    for (int i = 1; i < argc; ++i) {
        if (!argv[i] || std::strcmp(argv[i], "--resume") != 0) continue;
        std::string value;
        for (int j = i + 1; j < argc && argv[j] && std::strncmp(argv[j], "--", 2) != 0; ++j) {
            if (!value.empty()) value.push_back(' ');
            value += argv[j];
        }
        if (value.empty()) return std::nullopt;
        return value;
    }
    return std::nullopt;
}

}  // namespace emu
