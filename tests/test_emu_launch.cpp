#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include <string>
#include <vector>

#include "emu/LaunchLine.hpp"

using namespace emu;

static std::string lineFor(const std::string& file, const std::string& title) {
    const Console* snes = findConsole("snes");
    REQUIRE(snes != nullptr);
    ConsolePaths p = pathsFor(*snes);
    RomEntry r{p.roms + "/" + file, file, title};
    return runnerArgs(*snes, p, r, "sdmc:/switch/Equinox.nro", "1|snes|shelf|-1|" + file);
}

TEST_CASE("runnerArgs exact line") {
    CHECK(lineFor("Super Mario World (U) [!].smc", "Super Mario World (U) [!]") ==
          "--protocol 1 --rom \"sdmc:/equinox/roms/snes/Super Mario World (U) [!].smc\" "
          "--title \"Super Mario World (U) [!]\" --saves \"sdmc:/equinox/saves/snes\" "
          "--states \"sdmc:/equinox/states/snes\" --return \"sdmc:/switch/Equinox.nro\" "
          "--resume \"1|snes|shelf|-1|Super Mario World (U) [!].smc\"");
}

TEST_CASE("runnerArgs sanitizes") {
    std::string l = lineFor("a.sfc", "Bad \"T\"\nitle");
    CHECK(l.find("--title \"Bad Title\"") != std::string::npos);
    CHECK(lineFor("a.sfc", "Pok\xC3\xA9mon d'Or").find("--title \"Pok\xC3\xA9mon d'Or\"") != std::string::npos);
    CHECK(sanitizeArg(std::string("a\"b\x01\x7f" "c")) == "abc");
}

TEST_CASE("returnPath") {
    CHECK(returnPath("sdmc:/switch/Equinox.nro") == "sdmc:/switch/Equinox.nro");
    CHECK(returnPath("sdmc:/switch/apps/Eqx.nro") == "sdmc:/switch/apps/Eqx.nro");
    CHECK(returnPath("sdmc:/switch/X.NRO") == "sdmc:/switch/X.NRO");
    CHECK(returnPath("") == "sdmc:/switch/Equinox.nro");
    CHECK(returnPath("romfs:/x") == "sdmc:/switch/Equinox.nro");
    CHECK(returnPath("sdmc:/switch/Equinox.elf") == "sdmc:/switch/Equinox.nro");
}

TEST_CASE("launch path normalization") {
    CHECK(normalizeLaunchPath("/switch/Equinox/Equinox.nro") == "sdmc:/switch/Equinox/Equinox.nro");
    CHECK(normalizeLaunchPath("sdmc:/switch/Equinox.nro") == "sdmc:/switch/Equinox.nro");
    CHECK(normalizeLaunchPath("") == "");
    CHECK(normalizeLaunchPath("romfs:/x") == "romfs:/x");
    CHECK(returnPath("/switch/apps/Eqx.nro") == "sdmc:/switch/apps/Eqx.nro");
    CHECK(returnPath("/switch/Equinox.elf") == "sdmc:/switch/Equinox.nro");
}

TEST_CASE("return candidates and pick") {
    using V = std::vector<std::string>;
    CHECK(returnCandidates("/switch/apps/Eqx.nro") ==
          V{"sdmc:/switch/apps/Eqx.nro", "sdmc:/switch/Equinox/Equinox.nro", "sdmc:/switch/Equinox.nro"});
    CHECK(returnCandidates("sdmc:/switch/Equinox/Equinox.nro") ==
          V{"sdmc:/switch/Equinox/Equinox.nro", "sdmc:/switch/Equinox.nro"});
    CHECK(returnCandidates("sdmc:/switch/Equinox.nro") ==
          V{"sdmc:/switch/Equinox.nro", "sdmc:/switch/Equinox/Equinox.nro"});
    CHECK(returnCandidates("") == V{"sdmc:/switch/Equinox/Equinox.nro", "sdmc:/switch/Equinox.nro"});
    CHECK(returnCandidates("romfs:/x") == V{"sdmc:/switch/Equinox/Equinox.nro", "sdmc:/switch/Equinox.nro"});

    auto only = [](const std::string& path) {
        return [path](const std::string& p) { return p == path; };
    };
    CHECK(pickReturnPath("/switch/apps/Eqx.nro", only("sdmc:/switch/apps/Eqx.nro")) == "sdmc:/switch/apps/Eqx.nro");
    // argv[0] gone (moved): the README location.
    CHECK(pickReturnPath("/switch/apps/Eqx.nro", only("sdmc:/switch/Equinox/Equinox.nro")) ==
          "sdmc:/switch/Equinox/Equinox.nro");
    CHECK(pickReturnPath("", only("sdmc:/switch/Equinox.nro")) == "sdmc:/switch/Equinox.nro");
    CHECK(!pickReturnPath("/switch/apps/Eqx.nro", [](const std::string&) { return false; }));
}

TEST_CASE("token round trip and rejects") {
    ResumeToken t{"snes", Origin::Favorites, 2, "a b.sfc"};
    auto d = decodeToken(encodeToken(t));
    REQUIRE(d.has_value());
    CHECK(d->console == "snes");
    CHECK(d->origin == Origin::Favorites);
    CHECK(d->filter == 2);
    CHECK(d->romFile == "a b.sfc");
    CHECK(!decodeToken("2|snes|shelf|-1|x.sfc"));
    CHECK(!decodeToken(""));
    CHECK(!decodeToken("1|snes"));
    CHECK(!decodeToken("1|snes|other|-1|x"));
    CHECK(!decodeToken("1|snes|shelf|abc|x"));
    CHECK(!decodeToken("1|snes|shelf|-2|x"));
    CHECK(!decodeToken("1|snes|shelf|1x|x"));
    CHECK(!decodeToken("1|snes|shelf|-1|x|y"));
}

TEST_CASE("resumeArg") {
    const char* a[] = {"Equinox.nro", "--resume", "1|snes|shelf|-1|a b.sfc"};
    CHECK(resumeArg(3, a) == std::string("1|snes|shelf|-1|a b.sfc"));
    const char* b[] = {"Equinox.nro", "--resume"};
    CHECK(!resumeArg(2, b));
    const char* c[] = {"x", "--mac=..", "--appid=1"};
    CHECK(!resumeArg(3, c));
    const char* d[] = {"x", "--resume=1|snes|shelf|-1|a"};
    CHECK(!resumeArg(2, d));
    const char* e[] = {"x", "--resume", "1|snes|shelf|-1|a", "b", "c.sfc", "--mac=1"};
    CHECK(resumeArg(6, e) == std::string("1|snes|shelf|-1|a b c.sfc"));
}
