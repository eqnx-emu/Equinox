// The update's files on the card, without the app: shared with the host tests.

#include "Updater.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <vector>

namespace Updater {

namespace {

std::vector<int> parseVersion(std::string version) {
    if (!version.empty() && (version[0] == 'v' || version[0] == 'V'))
        version.erase(0, 1);

    std::vector<int> parts;
    std::stringstream stream(version);
    std::string part;
    while (std::getline(stream, part, '.')) {
        try {
            parts.push_back(std::stoi(part));
        } catch (...) {
            return {};
        }
    }
    return parts;
}

bool exists(const std::string& path) {
    FILE* file = fopen(path.c_str(), "rb");
    if (!file)
        return false;
    fclose(file);
    return true;
}

std::string readVersion(const std::string& path) {
    FILE* file = fopen(path.c_str(), "rb");
    if (!file)
        return "";
    char buffer[64] = {};
    const size_t read = fread(buffer, 1, sizeof(buffer) - 1, file);
    fclose(file);
    std::string version(buffer, read);
    while (!version.empty() && (version.back() == '\n' || version.back() == '\r' ||
                                version.back() == ' '))
        version.pop_back();
    return version;
}

} // namespace

bool isNewer(const std::string& candidate, const std::string& current) {
    std::vector<int> a = parseVersion(candidate);
    std::vector<int> b = parseVersion(current);
    if (a.empty() || b.empty())
        return false;

    size_t count = std::max(a.size(), b.size());
    a.resize(count, 0);
    b.resize(count, 0);
    return a > b;
}

std::string stagedVersionPath(const std::string& target) {
    return target + ".new.version";
}

bool looksLikeNro(const std::string& path, size_t expectedSize) {
    FILE* file = fopen(path.c_str(), "rb");
    if (!file)
        return false;

    char header[0x14] = {};
    bool ok = fread(header, 1, sizeof(header), file) == sizeof(header) &&
              memcmp(header + 0x10, "NRO0", 4) == 0;
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fclose(file);
    return ok && size > 0 && (expectedSize == 0 || (size_t)size == expectedSize);
}

void clearStaged(const std::string& target) {
    remove((target + ".new").c_str());
    remove(stagedVersionPath(target).c_str());
}

bool stage(const std::string& target, const std::string& version) {
    const std::string staged = target + ".new";
    clearStaged(target);
    if (rename((staged + ".part").c_str(), staged.c_str()) != 0)
        return false;

    FILE* file = fopen(stagedVersionPath(target).c_str(), "wb");
    bool ok = file && fwrite(version.data(), 1, version.size(), file) == version.size();
    if (file && fclose(file) != 0)
        ok = false;
    if (!ok)
        clearStaged(target);
    return ok;
}

std::optional<std::string> stagedUpdate(const std::string& target,
                                        const std::string& current) {
    const std::string staged = target + ".new";
    const std::string versionPath = stagedVersionPath(target);
    if (!exists(staged) && !exists(versionPath))
        return std::nullopt;

    const std::string version = readVersion(versionPath);
    if (!version.empty() && isNewer(version, current) && looksLikeNro(staged, 0))
        return version;
    clearStaged(target);
    return std::nullopt;
}

} // namespace Updater
