#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>

#include "emu/RunnerInstaller.hpp"

using namespace emu;
namespace fs = std::filesystem;

// Fake transport: RunnerInstaller.cpp links against these instead of curl.
namespace fake {
std::string body;   // what a download returns
bool fail = false;  // the transfer fails after writing the whole body
bool writeFail = false;  // the .part cannot be written (full card)
bool holdPart = false;  // keep the downloaded file open after the download
long failStatus = 0;  // the HTTP status a failed transfer reports
std::unique_ptr<std::ifstream> held;
std::string lastUrl;
}  // namespace fake

bool httpGetToString(const std::string& url, std::string& out, long, std::string& error) {
    fake::lastUrl = url;
    if (fake::fail) {
        error = "network down";
        return false;
    }
    out = fake::body;
    return true;
}

ResumableResult httpGetResumable(const std::string& url, const std::string& partPath,
                                 const Progress& progress) {
    fake::lastUrl = url;
    if (fake::writeFail) return {false, true, false, 0, "could not write " + partPath};
    std::ofstream f(partPath, std::ios::binary);
    uint64_t total = fake::body.size();
    f.write(fake::body.data(), static_cast<std::streamsize>(total / 2));
    f.flush();
    if (progress && !progress(total / 2, total)) return {false, false, false, 200, "cancelled"};
    f.write(fake::body.data() + total / 2, static_cast<std::streamsize>(total - total / 2));
    if (fake::fail) return {false, false, false, fake::failStatus, "network down"};
    f.close();
    if (fake::holdPart) fake::held = std::make_unique<std::ifstream>(partPath, std::ios::binary);
    return {true, false, false, 200, {}};
}

static const char* kHelloSha = "2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824";

static std::string readAll(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream s;
    s << f.rdbuf();
    return s.str();
}

static void writeAll(const fs::path& p, const std::string& data) {
    std::ofstream f(p, std::ios::binary);
    f << data;
}

struct Sandbox {
    fs::path root = fs::temp_directory_path() / "equinox_emu_installer";
    const Console& snes = *findConsole("snes");
    ConsolePaths paths;
    Manifest manifest;

    Sandbox() {
        fs::remove_all(root);
        paths = pathsFor(snes, root.string());
        manifest.id = "snes9x";
        manifest.console = "snes";
        manifest.version = "1.2.0";
        manifest.protocol = 1;
        manifest.asset = "equinox-snes9x.nro";
        manifest.size = 5;
        manifest.sha256 = kHelloSha;
        fake::body = "hello";
        fake::fail = false;
        fake::writeFail = false;
        fake::holdPart = false;
        fake::failStatus = 0;
        fake::held.reset();
        fake::lastUrl.clear();
    }
    ~Sandbox() {
        fake::held.reset();
        fs::remove_all(root);
    }

    void installOld() {
        REQUIRE(ensureFolders(paths));
        writeAll(paths.runner, "old");
    }
};

TEST_CASE("installRunner replaces runner.bin and records installed.json") {
    Sandbox s;
    s.installOld();
    uint64_t lastDone = 0;
    InstallResult r = installRunner(
        s.snes, s.manifest,
        [&](uint64_t done, uint64_t) {
            lastDone = done;
            return true;
        },
        s.root.string());
    CHECK(r.ok);
    CHECK(r.error.empty());
    CHECK(fake::lastUrl == assetUrl(s.snes, s.manifest));
    CHECK(lastDone > 0);
    CHECK(readAll(s.paths.runner) == "hello");
    CHECK_FALSE(r.cancelled);
    CHECK_FALSE(fs::exists(s.paths.runner + ".part"));
    CHECK_FALSE(fs::exists(s.paths.runner + ".old"));
    auto installed = readInstalled(s.paths.installed);
    REQUIRE(installed.has_value());
    CHECK(installed->version == "1.2.0");
    CHECK(installed->sha256 == kHelloSha);
    CHECK(installed->protocol == 1);
    CHECK(installed->lastCheck > 0);
}

TEST_CASE("installRunner creates the folders on a fresh card") {
    Sandbox s;
    InstallResult r = installRunner(s.snes, s.manifest, Progress(), s.root.string());
    CHECK(r.ok);
    CHECK(fs::is_directory(s.paths.roms));
    CHECK(fs::is_directory(s.paths.covers));
    CHECK(readAll(s.paths.runner) == "hello");
}

TEST_CASE("installRunner keeps the old runner when the hash does not match") {
    Sandbox s;
    s.installOld();
    s.manifest.sha256 = std::string(64, '0');
    InstallResult r = installRunner(s.snes, s.manifest, Progress(), s.root.string());
    CHECK_FALSE(r.ok);
    CHECK_FALSE(r.error.empty());
    CHECK(r.code == RunnerError::Mismatch);
    CHECK(readAll(s.paths.runner) == "old");
    CHECK_FALSE(fs::exists(s.paths.runner + ".part"));
    CHECK_FALSE(fs::exists(s.paths.installed));
}

TEST_CASE("installRunner keeps the old runner when the size does not match") {
    Sandbox s;
    s.installOld();
    s.manifest.size = 6;
    CHECK_FALSE(installRunner(s.snes, s.manifest, Progress(), s.root.string()).ok);
    CHECK(readAll(s.paths.runner) == "old");
    CHECK_FALSE(fs::exists(s.paths.runner + ".part"));
}

TEST_CASE("installRunner keeps the .part after a network failure") {
    Sandbox s;
    s.installOld();
    fake::fail = true;
    InstallResult r = installRunner(s.snes, s.manifest, Progress(), s.root.string());
    CHECK_FALSE(r.ok);
    CHECK(r.error == "network down");
    CHECK(r.code == RunnerError::Network);
    CHECK_FALSE(r.cancelled);
    CHECK(readAll(s.paths.runner) == "old");
    // The next attempt in this session continues it.
    CHECK(fs::exists(s.paths.runner + ".part"));
}

TEST_CASE("installRunner reports the HTTP status of a failed download") {
    Sandbox s;
    fake::fail = true;
    fake::failStatus = 404;
    InstallResult r = installRunner(s.snes, s.manifest, Progress(), s.root.string());
    CHECK(r.code == RunnerError::Network);
    CHECK(r.httpStatus == 404);
}

TEST_CASE("installRunner reports a full card as a disk error") {
    Sandbox s;
    s.installOld();
    fake::writeFail = true;
    InstallResult r = installRunner(s.snes, s.manifest, Progress(), s.root.string());
    CHECK_FALSE(r.ok);
    CHECK(r.code == RunnerError::Disk);
    CHECK(readAll(s.paths.runner) == "old");
}

TEST_CASE("installRunner keeps the .part when stopped, and discarding removes it") {
    Sandbox s;
    s.installOld();
    InstallResult r = installRunner(
        s.snes, s.manifest, [](uint64_t, uint64_t) { return false; }, s.root.string());
    CHECK_FALSE(r.ok);
    CHECK(r.error == "cancelled");
    CHECK(r.code == RunnerError::Cancelled);
    CHECK(r.cancelled);
    CHECK(readAll(s.paths.runner) == "old");
    CHECK(fs::exists(s.paths.runner + ".part"));

    writeAll(s.paths.runner + ".part.resume", "{}");
    discardRunnerDownload(s.snes, s.root.string());
    CHECK_FALSE(fs::exists(s.paths.runner + ".part"));
    CHECK_FALSE(fs::exists(s.paths.runner + ".part.resume"));
    CHECK(readAll(s.paths.runner) == "old");
}

TEST_CASE("installRunner clears a stale runner.bin.old") {
    Sandbox s;
    s.installOld();
    writeAll(s.paths.runner + ".old", "stale");
    CHECK(installRunner(s.snes, s.manifest, Progress(), s.root.string()).ok);
    CHECK(readAll(s.paths.runner) == "hello");
    CHECK_FALSE(fs::exists(s.paths.runner + ".old"));
}

TEST_CASE("installRunner leaves runner.bin alone when the backup name is taken") {
    Sandbox s;
    s.installOld();
    fs::create_directories(s.paths.runner + ".old/keep");
    InstallResult r = installRunner(s.snes, s.manifest, Progress(), s.root.string());
    CHECK_FALSE(r.ok);
    CHECK(readAll(s.paths.runner) == "old");
    CHECK_FALSE(fs::exists(s.paths.runner + ".part"));
}

#ifdef _WIN32
// Windows will not rename a file another handle has open, which makes the
// move of the new runner into place fail after the old one was backed up.
TEST_CASE("installRunner restores the old runner when the new one cannot be moved in") {
    Sandbox s;
    s.installOld();
    fake::holdPart = true;
    InstallResult r = installRunner(s.snes, s.manifest, Progress(), s.root.string());
    CHECK_FALSE(r.ok);
    CHECK(r.error == "could not move the runner into place");
    CHECK(r.code == RunnerError::Move);
    CHECK(readAll(s.paths.runner) == "old");
    CHECK_FALSE(fs::exists(s.paths.runner + ".old"));
    CHECK_FALSE(fs::exists(s.paths.installed));
}
#endif

TEST_CASE("removeRunner deletes only the emulator folder") {
    Sandbox s;
    s.installOld();
    writeAll(fs::path(s.paths.roms) / "game.sfc", "rom");
    writeAll(fs::path(s.paths.saves) / "game.srm", "save");
    CHECK(removeRunner(s.snes, s.root.string()));
    CHECK_FALSE(fs::exists(s.paths.emulator));
    CHECK(fs::exists(fs::path(s.paths.roms) / "game.sfc"));
    CHECK(fs::exists(fs::path(s.paths.saves) / "game.srm"));
    CHECK(fs::is_directory(s.paths.states));
    CHECK(fs::is_directory(s.paths.covers));
}

TEST_CASE("fetchLatest parses the manifest of the right console") {
    Sandbox s;
    fake::body =
        R"({"id":"snes9x","console":"snes","version":"1.0.0","protocol":1,"asset":"a.nro","size":1,"sha256":"x"})";
    std::string error;
    auto m = fetchLatest(s.snes, error);
    REQUIRE(m.has_value());
    CHECK(m->version == "1.0.0");
    CHECK(fake::lastUrl == manifestUrl(s.snes));
}

TEST_CASE("fetchLatest rejects bad manifests and network errors") {
    Sandbox s;
    std::string error;
    RunnerError code = RunnerError::None;

    fake::body =
        R"({"id":"x","console":"nes","version":"1.0.0","protocol":1,"asset":"a.nro","size":1,"sha256":"x"})";
    CHECK_FALSE(fetchLatest(s.snes, error, &code).has_value());
    CHECK_FALSE(error.empty());
    CHECK(code == RunnerError::WrongConsole);

    error.clear();
    fake::body = "not json";
    CHECK_FALSE(fetchLatest(s.snes, error, &code).has_value());
    CHECK_FALSE(error.empty());
    CHECK(code == RunnerError::BadManifest);

    error.clear();
    fake::fail = true;
    CHECK_FALSE(fetchLatest(s.snes, error, &code).has_value());
    CHECK(error == "network down");
    CHECK(code == RunnerError::Network);
}
