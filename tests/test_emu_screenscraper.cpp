#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include <string>

#include "emu/ScreenScraper.hpp"

using namespace emu;

static const char* kBoth =
    R"({"header":{"APIversion":"2.0"},"response":{"jeu":{"synopsis":[)"
    R"({"langue":"en","text":"English text"},{"langue":"pt","text":"Texto em portugues"}]}}})";
static const char* kOnlyEn =
    R"({"header":{},"response":{"jeu":{"synopsis":[{"langue":"en","text":"English text"}]}}})";
static const char* kOnlyFr =
    R"({"header":{},"response":{"jeu":{"synopsis":[{"langue":"fr","text":"Texte"}]}}})";

static SsConfig cfg() { return {"dev", "pass", "Equinox", "me", "secret", "pt"}; }

TEST_CASE("ssConfigured needs dev and user credentials") {
    CHECK(ssConfigured(cfg()));
    SsConfig c = cfg();
    c.devId.clear();
    CHECK_FALSE(ssConfigured(c));
    c = cfg();
    c.devPassword.clear();
    CHECK_FALSE(ssConfigured(c));
    c = cfg();
    c.user.clear();
    CHECK_FALSE(ssConfigured(c));
    c = cfg();
    c.password.clear();
    CHECK_FALSE(ssConfigured(c));
}

TEST_CASE("ssSystemId maps snes only") {
    Console snes;
    snes.slug = "snes";
    CHECK(ssSystemId(snes) == 4);
    Console other;
    other.slug = "nes";
    CHECK(ssSystemId(other) == 0);
}

TEST_CASE("ssUrl encodes every field") {
    SsQuery q{4, 0xB19ED489u, "Super Mario World (USA).sfc", 524288};
    const std::string u = ssUrl(cfg(), q);
    CHECK(u.rfind("https://api.screenscraper.fr/api2/jeuInfos.php?", 0) == 0);
    CHECK(u.find("systemeid=4") != std::string::npos);
    CHECK(u.find("crc=B19ED489") != std::string::npos);
    CHECK(u.find("romnom=Super%20Mario%20World%20%28USA%29.sfc") != std::string::npos);
    CHECK(u.find("romtaille=524288") != std::string::npos);
    CHECK(u.find("romtype=rom") != std::string::npos);
    CHECK(u.find("output=json") != std::string::npos);
    CHECK(u.find("devid=dev") != std::string::npos);
    CHECK(u.find("devpassword=pass") != std::string::npos);
    CHECK(u.find("softname=Equinox") != std::string::npos);
    CHECK(u.find("ssid=me") != std::string::npos);
    CHECK(u.find("sspassword=secret") != std::string::npos);
}

TEST_CASE("ssUrl pads the crc and escapes credentials") {
    SsConfig c = cfg();
    c.password = "a&b c";
    SsQuery q{4, 0xABu, "x", 1};
    const std::string u = ssUrl(c, q);
    CHECK(u.find("crc=000000AB") != std::string::npos);
    CHECK(u.find("sspassword=a%26b%20c") != std::string::npos);
}

TEST_CASE("ssParse picks the requested language") {
    SsResult r = ssParse(200, kBoth, "pt");
    CHECK(r.status == SsStatus::Ok);
    CHECK(r.synopsis == "Texto em portugues");
    CHECK(r.lang == "pt");
}

TEST_CASE("ssParse falls back to en, then to the first") {
    SsResult r = ssParse(200, kOnlyEn, "pt");
    CHECK(r.status == SsStatus::Ok);
    CHECK(r.synopsis == "English text");
    CHECK(r.lang == "en");
    r = ssParse(200, kOnlyFr, "pt");
    CHECK(r.status == SsStatus::Ok);
    CHECK(r.synopsis == "Texte");
    CHECK(r.lang == "fr");
}

TEST_CASE("ssParse accepts a game without synopsis") {
    SsResult r = ssParse(200, R"({"response":{"jeu":{}}})", "en");
    CHECK(r.status == SsStatus::Ok);
    CHECK(r.synopsis.empty());
}

TEST_CASE("ssParse maps http statuses") {
    CHECK(ssParse(404, "Erreur : Rom/Iso/Dossier non trouv\xC3\xA9" "e !", "pt").status == SsStatus::NotFound);
    CHECK(ssParse(403, "", "pt").status == SsStatus::BadLogin);
    for (long s : {401, 423}) CHECK(ssParse(s, "", "pt").status == SsStatus::Transient);
    for (long s : {426, 429, 430, 431}) CHECK(ssParse(s, "", "pt").status == SsStatus::Quota);
    CHECK(ssParse(0, "", "pt").status == SsStatus::Transient);
    CHECK(ssParse(500, "", "pt").status == SsStatus::Transient);
}

TEST_CASE("ssParse treats unusable 200 bodies as transient") {
    CHECK(ssParse(200, "<html>", "pt").status == SsStatus::Transient);
    CHECK(ssParse(200, R"({"response":{}})", "pt").status == SsStatus::Transient);
    CHECK(ssParse(200, "", "pt").status == SsStatus::Transient);
}
