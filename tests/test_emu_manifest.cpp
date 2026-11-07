#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include <filesystem>
#include <fstream>
#include <string>

#include "emu/RunnerManifest.hpp"

using namespace emu;
namespace fs = std::filesystem;

static const char* kRealManifest =
    R"({"id": "snes9x", "console": "snes", "version": "1.0.0", "protocol": 1, "asset": "equinox-snes9x.nro", "size": 11391141, "sha256": "bb1c0f6f93835bc5732df1a36f8481fbcc5468eacd16312c8c12994ab511d4ed"})";

static Manifest latest(const std::string& version, int protocol = 1) {
    Manifest m = *parseManifest(kRealManifest);
    m.version = version;
    m.protocol = protocol;
    return m;
}

static Installed inst(const std::string& version) {
    Installed i;
    i.version = version;
    i.protocol = 1;
    return i;
}

static fs::path tempPath(const std::string& name) {
    return fs::temp_directory_path() / ("equinox_emu_manifest_" + name);
}

TEST_CASE("parseManifest reads the real v1.0.0 manifest") {
    auto m = parseManifest(kRealManifest);
    REQUIRE(m.has_value());
    CHECK(m->id == "snes9x");
    CHECK(m->console == "snes");
    CHECK(m->version == "1.0.0");
    CHECK(m->protocol == 1);
    CHECK(m->asset == "equinox-snes9x.nro");
    CHECK(m->size == 11391141);
    CHECK(m->sha256 == "bb1c0f6f93835bc5732df1a36f8481fbcc5468eacd16312c8c12994ab511d4ed");
}

TEST_CASE("parseManifest rejects bad input") {
    CHECK_FALSE(parseManifest("not json").has_value());
    CHECK_FALSE(parseManifest("").has_value());
    CHECK_FALSE(parseManifest(R"({"id":"a","console":"snes","version":"1.0.0","protocol":"1","asset":"x","size":1,"sha256":"y"})").has_value());
    CHECK_FALSE(parseManifest(R"({"id":"a","console":"snes","version":"1.0.0","protocol":1,"asset":"x","sha256":"y"})").has_value());
}

TEST_CASE("compareVersions") {
    CHECK(compareVersions("1.10.0", "1.9.2") == 1);
    CHECK(compareVersions("1.9.2", "1.10.0") == -1);
    CHECK(compareVersions("1.0.0", "1.0.0") == 0);
    CHECK(compareVersions("1.0", "1.0.1") == -1);
    CHECK(compareVersions("1.0", "1.0.0") == 0);
}

TEST_CASE("decide") {
    CHECK(decide(std::nullopt, latest("1.0.0"), 1) == RunnerAction::Install);
    CHECK(decide(inst("1.0.0"), latest("1.0.1"), 1) == RunnerAction::Update);
    CHECK(decide(inst("1.0.1"), latest("1.0.1"), 1) == RunnerAction::UpToDate);
    CHECK(decide(inst("1.0.0"), latest("1.0.1", 2), 1) == RunnerAction::NeedsNewerEquinox);
    CHECK(decide(std::nullopt, latest("2.0.0", 2), 1) == RunnerAction::NeedsNewerEquinox);
    CHECK(decide(inst("1.2.0"), latest("1.0.1"), 1) == RunnerAction::UpToDate);
}

TEST_CASE("shouldCheck") {
    const int64_t now = 1800000000;
    CHECK(shouldCheck(0, now));
    CHECK_FALSE(shouldCheck(now - 3600, now));
    CHECK(shouldCheck(now - 86400, now));
    CHECK(shouldCheck(now + 100, now));
}

TEST_CASE("installed round trip and corruption") {
    fs::path p = tempPath("installed.json");
    std::error_code ec;
    fs::remove(p, ec);

    Installed i;
    i.version = "1.0.0";
    i.sha256 = "abcd";
    i.protocol = 1;
    i.lastCheck = 1800000123;
    REQUIRE(writeInstalled(p.string(), i));
    CHECK_FALSE(fs::exists(p.string() + ".tmp"));
    auto back = readInstalled(p.string());
    REQUIRE(back.has_value());
    CHECK(back->version == i.version);
    CHECK(back->sha256 == i.sha256);
    CHECK(back->protocol == i.protocol);
    CHECK(back->lastCheck == i.lastCheck);

    {
        std::ofstream(p, std::ios::binary | std::ios::trunc) << "{ broken";
    }
    CHECK_FALSE(readInstalled(p.string()).has_value());
    CHECK_FALSE(readInstalled(tempPath("missing.json").string()).has_value());

    fs::remove(p, ec);
}

TEST_CASE("writeInstalled replaces an existing file") {
    fs::path p = tempPath("installed_twice.json");
    std::error_code ec;
    fs::remove(p, ec);
    Installed a = inst("1.0.0");
    a.lastCheck = 10;
    Installed b = inst("1.0.1");
    b.lastCheck = 20;
    REQUIRE(writeInstalled(p.string(), a));
    REQUIRE(writeInstalled(p.string(), b));
    auto back = readInstalled(p.string());
    REQUIRE(back.has_value());
    CHECK(back->version == "1.0.1");
    CHECK(back->lastCheck == 20);
    CHECK_FALSE(fs::exists(p.string() + ".tmp"));
    fs::remove(p, ec);
}

TEST_CASE("parseManifest rejects out-of-range protocol") {
    CHECK_FALSE(parseManifest(R"({"id":"a","console":"snes","version":"1.0.0","protocol":4294967297,"asset":"x","size":1,"sha256":"y"})").has_value());
}

TEST_CASE("verifyFile accepts an uppercase expected sha") {
    fs::path p = tempPath("abc_upper.bin");
    std::error_code ec;
    {
        std::ofstream(p, std::ios::binary | std::ios::trunc) << "abc";
    }
    Manifest m;
    m.size = 3;
    m.sha256 = "BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD";
    CHECK(verifyFile(p.string(), m));
    fs::remove(p, ec);
}

TEST_CASE("verifyFile") {
    fs::path p = tempPath("abc.bin");
    std::error_code ec;
    {
        std::ofstream(p, std::ios::binary | std::ios::trunc) << "abc";
    }
    Manifest m;
    m.size = 3;
    m.sha256 = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
    CHECK(sha256Hex(p.string()) == m.sha256);
    CHECK(verifyFile(p.string(), m));

    Manifest wrongSize = m;
    wrongSize.size = 4;
    CHECK_FALSE(verifyFile(p.string(), wrongSize));

    Manifest wrongSha = m;
    wrongSha.sha256 = std::string(64, '0');
    CHECK_FALSE(verifyFile(p.string(), wrongSha));

    fs::remove(p, ec);
    CHECK(sha256Hex(p.string()) == "");
    CHECK_FALSE(verifyFile(p.string(), m));
}

TEST_CASE("urls") {
    const Console* snes = findConsole("snes");
    REQUIRE(snes != nullptr);
    Manifest m = *parseManifest(kRealManifest);
    CHECK(manifestUrl(*snes) ==
          "https://github.com/luizfbalves/equinox-snes9x/releases/latest/download/manifest.json");
    CHECK(assetUrl(*snes, m) ==
          "https://github.com/luizfbalves/equinox-snes9x/releases/download/v1.0.0/equinox-snes9x.nro");
}
