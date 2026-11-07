#include "emu/RunnerInstaller.hpp"

#include "emu/PathUtf8.hpp"

#include <ctime>
#include <filesystem>
#include <system_error>

namespace emu {

namespace {

namespace fs = std::filesystem;

const long MANIFEST_TIMEOUT = 15;
const long RUNNER_TIMEOUT = 300;

// Horizon's FAT driver will not rename over an existing file, so the target
// is removed and the rename tried again.
bool replaceFile(const fs::path& from, const fs::path& to) {
    std::error_code ec;
    fs::rename(from, to, ec);
    if (ec && fs::exists(to)) {
        ec.clear();
        fs::remove(to, ec);
        if (ec) return false;
        fs::rename(from, to, ec);
    }
    return !ec;
}

InstallResult fail(const fs::path& part, const Manifest& manifest, const std::string& error) {
    std::error_code ec;
    fs::remove(part, ec);
    return {false, error, manifest};
}

}  // namespace

std::optional<Manifest> fetchLatest(const Console& console, std::string& error) {
    std::string body;
    if (!httpGetToString(manifestUrl(console), body, MANIFEST_TIMEOUT, error)) return std::nullopt;

    std::optional<Manifest> manifest = parseManifest(body);
    if (!manifest) {
        error = "invalid runner manifest";
        return std::nullopt;
    }
    if (manifest->console != console.slug) {
        error = "manifest is for another console";
        return std::nullopt;
    }
    return manifest;
}

InstallResult installRunner(const Console& console, const Manifest& manifest,
                            const Progress& progress, const std::string& root) {
    const ConsolePaths paths = pathsFor(console, root);
    if (!ensureFolders(paths)) return {false, "could not create the folders", manifest};

    const std::string partPath = paths.runner + ".part";
    const fs::path part = fromUtf8(partPath);

    std::string error;
    if (!httpGetToFile(assetUrl(console, manifest), partPath, RUNNER_TIMEOUT, progress, error))
        return fail(part, manifest, error.empty() ? "download failed" : error);

    if (!verifyFile(partPath, manifest))
        return fail(part, manifest, "downloaded runner does not match the manifest");

    if (!replaceFile(part, fromUtf8(paths.runner)))
        return fail(part, manifest, "could not move the runner into place");

    Installed installed;
    installed.version = manifest.version;
    installed.sha256 = manifest.sha256;
    installed.protocol = manifest.protocol;
    installed.lastCheck = static_cast<int64_t>(std::time(nullptr));
    if (!writeInstalled(paths.installed, installed))
        return {false, "could not write installed.json", manifest};

    return {true, "", manifest};
}

bool removeRunner(const Console& console, const std::string& root) {
    std::error_code ec;
    fs::remove_all(fromUtf8(pathsFor(console, root).emulator), ec);
    return !ec;
}

}  // namespace emu
