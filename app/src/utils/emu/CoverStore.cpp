#include "emu/CoverStore.hpp"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <system_error>

#include "emu/PathUtf8.hpp"

namespace fs = std::filesystem;

namespace emu {

std::string coverMissDir(const ConsolePaths& paths) { return paths.covers + "/.miss"; }

std::string coverMissPath(const ConsolePaths& paths, const std::string& title) {
    return coverMissDir(paths) + "/" + title;
}

bool coverMissed(const ConsolePaths& paths, const std::string& title, const std::string& version) {
    std::ifstream file(fromUtf8(coverMissPath(paths, title)), std::ios::binary);
    if (!file) return false;
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    return content == version;
}

bool markCoverMissed(const ConsolePaths& paths, const std::string& title, const std::string& version) {
    std::error_code ec;
    fs::create_directories(fromUtf8(coverMissDir(paths)), ec);
    std::ofstream file(fromUtf8(coverMissPath(paths, title)), std::ios::binary | std::ios::trunc);
    if (!file) return false;
    file << version;
    file.close();
    return !file.fail();
}

void clearCoverMiss(const ConsolePaths& paths, const std::string& title) {
    std::error_code ec;
    fs::remove(fromUtf8(coverMissPath(paths, title)), ec);
}

void clearCoverMisses(const ConsolePaths& paths) {
    std::error_code ec;
    fs::remove_all(fromUtf8(coverMissDir(paths)), ec);
}

bool looksLikePng(const std::string& path) {
    static const unsigned char signature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    std::ifstream file(fromUtf8(path), std::ios::binary);
    unsigned char head[8] = {};
    if (!file.read(reinterpret_cast<char*>(head), sizeof(head))) return false;
    for (int i = 0; i < 8; ++i)
        if (head[i] != signature[i]) return false;
    return true;
}

bool looksLikeImage(const std::string& path) {
    if (looksLikePng(path)) return true;
    std::ifstream file(fromUtf8(path), std::ios::binary);
    unsigned char head[3] = {};
    if (!file.read(reinterpret_cast<char*>(head), sizeof(head))) return false;
    return head[0] == 0xFF && head[1] == 0xD8 && head[2] == 0xFF;
}

std::string coverLookVersion(const std::string& appVersion, const std::string& sgdbKey) {
    if (sgdbKey.empty()) return appVersion;
    // A short FNV-1a of the key: tells keys apart without writing the key down.
    uint32_t hash = 2166136261u;
    for (unsigned char c : sgdbKey) {
        hash ^= c;
        hash *= 16777619u;
    }
    char tag[16];
    std::snprintf(tag, sizeof tag, "%08x", hash);
    return appVersion + "+sgdb-" + tag;
}

bool replaceFile(const std::string& from, const std::string& to) {
    std::error_code ec;
    fs::rename(fromUtf8(from), fromUtf8(to), ec);
    if (!ec) return true;
    fs::remove(fromUtf8(to), ec);
    fs::rename(fromUtf8(from), fromUtf8(to), ec);
    return !ec;
}

}  // namespace emu
