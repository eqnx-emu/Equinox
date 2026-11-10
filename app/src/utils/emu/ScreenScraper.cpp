#include "emu/ScreenScraper.hpp"

#include <jansson.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>

#include "emu/CoverName.hpp"

namespace emu {

bool ssConfigured(const SsConfig& cfg) {
    return !cfg.devId.empty() && !cfg.devPassword.empty() && !cfg.user.empty() &&
           !cfg.password.empty();
}

int ssSystemId(const Console& console) {
    if (console.slug == "snes") return 4;
    if (console.slug == "gba") return 12;
    if (console.slug == "gbc") return 10;
    return 0;
}

std::string ssUrl(const SsConfig& cfg, const SsQuery& q) {
    char crc[16];
    std::snprintf(crc, sizeof crc, "%08X", static_cast<unsigned>(q.crc));
    return "https://api.screenscraper.fr/api2/jeuInfos.php?devid=" + urlEncode(cfg.devId) +
           "&devpassword=" + urlEncode(cfg.devPassword) + "&softname=" + urlEncode(cfg.softName) +
           "&ssid=" + urlEncode(cfg.user) + "&sspassword=" + urlEncode(cfg.password) +
           "&output=json&romtype=rom&systemeid=" + std::to_string(q.systemId) + "&crc=" + crc +
           "&romnom=" + urlEncode(q.romName) + "&romtaille=" + std::to_string(q.romSize);
}

namespace {

std::string text(const json_t* object, const char* key) {
    const json_t* value = json_object_get(object, key);
    return json_is_string(value) ? json_string_value(value) : std::string();
}

// The item of a [{langue, text}] list in `lang`, else English, else the first.
const json_t* inLanguage(const json_t* list, const std::string& lang) {
    const json_t* first = nullptr;
    const json_t* en = nullptr;
    size_t i;
    const json_t* item;
    json_array_foreach(list, i, item) {
        if (!json_is_string(json_object_get(item, "text")) ||
            !json_is_string(json_object_get(item, "langue")))
            continue;
        const std::string code = text(item, "langue");
        if (code == lang) return item;
        if (!first) first = item;
        if (code == "en" && !en) en = item;
    }
    return en ? en : first;
}

// The last number in the text: "1-2" is 2, "4" is 4.
int lastNumber(const std::string& s) {
    int value = 0, current = -1;
    for (char c : s) {
        if (std::isdigit(static_cast<unsigned char>(c))) {
            current = (current < 0 ? 0 : current * 10) + (c - '0');
            if (current > 99) current = 99;
        } else if (current >= 0) {
            value = current;
            current = -1;
        }
    }
    return current >= 0 ? current : value;
}

int regionRank(const std::string& region) {
    static const char* order[] = {"wor", "us", "eu", "ss", "jp"};
    for (int i = 0; i < 5; i++)
        if (region == order[i]) return i;
    return 5;
}

// The URL of the best media of `type` in a format the app can show.
std::string mediaUrl(const json_t* medias, const char* type) {
    std::string best;
    int bestRank = 1000;
    size_t i;
    const json_t* item;
    json_array_foreach(medias, i, item) {
        if (text(item, "type") != type) continue;
        const std::string format = text(item, "format");
        if (format != "png" && format != "jpg" && format != "jpeg") continue;
        const std::string url = text(item, "url");
        if (url.rfind("https://", 0) != 0) continue;
        const int rank = regionRank(text(item, "region"));
        if (rank < bestRank) {
            bestRank = rank;
            best = url;
        }
    }
    return best;
}

void parseFacts(const json_t* jeu, const std::string& lang, SsResult& r) {
    // The main genre, else the first one.
    const json_t* genres = json_object_get(jeu, "genres");
    const json_t* genre = nullptr;
    size_t i;
    const json_t* item;
    json_array_foreach(genres, i, item) {
        if (!genre || text(item, "principale") == "1") genre = item;
        if (text(item, "principale") == "1") break;
    }
    if (const json_t* name = inLanguage(json_object_get(genre, "noms"), lang))
        r.genre = text(name, "text");
    r.developer = text(json_object_get(jeu, "developpeur"), "text");
    r.publisher = text(json_object_get(jeu, "editeur"), "text");
    r.players = lastNumber(text(json_object_get(jeu, "joueurs"), "text"));
    json_array_foreach(json_object_get(jeu, "dates"), i, item) {
        const std::string date = text(item, "text");
        if (date.size() < 4) continue;
        const int year = std::atoi(date.substr(0, 4).c_str());
        if (year >= 1950 && year <= 2100 && (r.year == 0 || year < r.year)) r.year = year;
    }
    const json_t* medias = json_object_get(jeu, "medias");
    r.snapUrl = mediaUrl(medias, "ss");
    r.titleUrl = mediaUrl(medias, "sstitle");
    r.boxUrl = mediaUrl(medias, "box-2D");
}

}  // namespace

static SsResult parseBody(const std::string& body, const std::string& lang) {
    SsResult r;
    json_error_t err;
    json_t* root = json_loadb(body.data(), body.size(), 0, &err);
    if (!root) return r;
    const json_t* jeu = json_object_get(json_object_get(root, "response"), "jeu");
    if (json_is_object(jeu)) {
        r.status = SsStatus::Ok;
        if (const json_t* pick = inLanguage(json_object_get(jeu, "synopsis"), lang)) {
            r.synopsis = text(pick, "text");
            r.lang = text(pick, "langue");
        }
        parseFacts(jeu, lang, r);
    }
    json_decref(root);
    return r;
}

SsResult ssParse(long httpStatus, const std::string& body, const std::string& lang) {
    SsResult r;
    switch (httpStatus) {
        case 200: return parseBody(body, lang);
        case 404: r.status = SsStatus::NotFound; break;
        case 403: r.status = SsStatus::BadLogin; break;
        // 401 and 423 mean the API is closed or overloaded, not that the login is wrong.
        case 401:
        case 423: r.status = SsStatus::Transient; break;
        case 426:
        case 429:
        case 430:
        case 431: r.status = SsStatus::Quota; break;
        default: break;
    }
    return r;
}

}  // namespace emu
