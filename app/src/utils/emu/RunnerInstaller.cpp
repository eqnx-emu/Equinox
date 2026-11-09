#include "emu/RunnerInstaller.hpp"

#include "emu/PathUtf8.hpp"
#include "emu/ReplaceFile.hpp"
#include "emu/ResumeState.hpp"

#include <ctime>
#include <filesystem>
#include <system_error>

namespace emu {

namespace {

namespace fs = std::filesystem;

const long MANIFEST_TIMEOUT = 15;

// Removes the .part: its bytes are no use to the next attempt.
InstallResult fail(const fs::path& part, const Manifest& manifest, RunnerError code,
                   const std::string& error) {
    std::error_code ec;
    fs::remove(part, ec);
    return {false, error, manifest, false, code};
}

}  // namespace

std::optional<Manifest> fetchLatest(const Console& console, std::string& error,
                                    RunnerError* code) {
    auto failWith = [&](RunnerError kind) -> std::optional<Manifest> {
        if (code) *code = kind;
        return std::nullopt;
    };
    if (code) *code = RunnerError::None;

    std::string body;
    if (!httpGetToString(manifestUrl(console), body, MANIFEST_TIMEOUT, error))
        return failWith(RunnerError::Network);

    std::optional<Manifest> manifest = parseManifest(body);
    if (!manifest) {
        error = "invalid runner manifest";
        return failWith(RunnerError::BadManifest);
    }
    if (manifest->console != console.slug) {
        error = "manifest is for another console";
        return failWith(RunnerError::WrongConsole);
    }
    return manifest;
}

InstallResult installRunner(const Console& console, const Manifest& manifest,
                            const Progress& progress, const std::string& root) {
    const ConsolePaths paths = pathsFor(console, root);
    if (!ensureFolders(paths))
        return {false, "could not create the folders", manifest, false, RunnerError::Folders};

    const std::string partPath = paths.runner + ".part";
    const fs::path part = fromUtf8(partPath);

    bool cancelled = false;
    Progress tracked;
    if (progress) {
        tracked = [&](uint64_t done, uint64_t total) {
            if (progress(done, total)) return true;
            cancelled = true;
            return false;
        };
    }

    const ResumableResult download = httpGetResumable(assetUrl(console, manifest), partPath, tracked);
    if (!download.ok) {
        // The .part stays: a later attempt continues it.
        const RunnerError code = cancelled              ? RunnerError::Cancelled
                                 : download.writeFailed ? RunnerError::Disk
                                                        : RunnerError::Network;
        return {false, download.error.empty() ? "download failed" : download.error, manifest,
                cancelled, code};
    }

    if (!verifyFile(partPath, manifest))
        return fail(part, manifest, RunnerError::Mismatch,
                    "downloaded runner does not match the manifest");

    if (!replaceKeepingBackup(part, fromUtf8(paths.runner), fromUtf8(paths.runner + ".old")))
        return fail(part, manifest, RunnerError::Move, "could not move the runner into place");

    Installed installed;
    installed.version = manifest.version;
    installed.sha256 = manifest.sha256;
    installed.protocol = manifest.protocol;
    installed.lastCheck = static_cast<int64_t>(std::time(nullptr));
    // The new runner is already in place; with installed.json stale, the next
    // decide() offers the update again, which fixes it.
    if (!writeInstalled(paths.installed, installed))
        return {false, "could not write installed.json", manifest, false,
                RunnerError::WriteInstalled};

    return {true, "", manifest};
}

void discardRunnerDownload(const Console& console, const std::string& root) {
    const std::string partPath = pathsFor(console, root).runner + ".part";
    std::error_code ec;
    fs::remove(fromUtf8(partPath), ec);
    removeResume(partPath);
}

bool removeRunner(const Console& console, const std::string& root) {
    std::error_code ec;
    fs::remove_all(fromUtf8(pathsFor(console, root).emulator), ec);
    return !ec;
}

}  // namespace emu
