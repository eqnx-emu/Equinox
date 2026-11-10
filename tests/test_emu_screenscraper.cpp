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

TEST_CASE("ssSystemId maps the known consoles") {
    Console snes;
    snes.slug = "snes";
    CHECK(ssSystemId(snes) == 4);
    Console gba;
    gba.slug = "gba";
    CHECK(ssSystemId(gba) == 12);
    Console gbc;
    gbc.slug = "gbc";
    CHECK(ssSystemId(gbc) == 10);
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

TEST_CASE("ssParse reads the facts and the media") {
    const char* body = R"({"response":{"jeu":{
        "synopsis":[{"langue":"en","text":"Text"}],
        "genres":[{"principale":"1","noms":[{"langue":"en","text":"Platform"},{"langue":"pt","text":"Plataforma"}]}],
        "developpeur":{"text":"Dev Co"},"editeur":{"text":"Pub Co"},"joueurs":{"text":"1-2"},
        "dates":[{"region":"us","text":"1993-08-01"},{"region":"jp","text":"1992-11-21"}],
        "medias":[
            {"type":"ss","region":"jp","format":"png","url":"https://ss.example/jp-snap"},
            {"type":"ss","region":"us","format":"png","url":"https://ss.example/us-snap"},
            {"type":"sstitle","region":"wor","format":"jpg","url":"https://ss.example/title"},
            {"type":"box-2D","region":"eu","format":"mp4","url":"https://ss.example/box-video"},
            {"type":"box-2D","region":"eu","format":"png","url":"http://ss.example/plain-http"}
        ]}}})";
    const SsResult r = ssParse(200, body, "pt");
    CHECK(r.status == SsStatus::Ok);
    CHECK(r.genre == "Plataforma");
    CHECK(r.developer == "Dev Co");
    CHECK(r.publisher == "Pub Co");
    CHECK(r.players == 2);
    CHECK(r.year == 1992);
    CHECK(r.snapUrl == "https://ss.example/us-snap");
    CHECK(r.titleUrl == "https://ss.example/title");
    CHECK(r.boxUrl.empty());  // a video and a plain http link are not covers
}

TEST_CASE("ssParse leaves missing facts empty") {
    const SsResult r = ssParse(200, R"({"response":{"jeu":{"joueurs":{"text":"?"}}}})", "en");
    CHECK(r.status == SsStatus::Ok);
    CHECK(r.genre.empty());
    CHECK(r.players == 0);
    CHECK(r.year == 0);
    CHECK(r.snapUrl.empty());
}
