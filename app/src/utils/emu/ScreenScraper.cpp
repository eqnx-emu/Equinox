#include "emu/ScreenScraper.hpp"

#include <jansson.h>

#include <cstdio>

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

static SsResult parseBody(const std::string& body, const std::string& lang) {
    SsResult r;
    json_error_t err;
    json_t* root = json_loadb(body.data(), body.size(), 0, &err);
    if (!root) return r;
    const json_t* jeu = json_object_get(json_object_get(root, "response"), "jeu");
    if (json_is_object(jeu)) {
        r.status = SsStatus::Ok;
        const json_t* list = json_object_get(jeu, "synopsis");
        const json_t* first = nullptr;
        const json_t* en = nullptr;
        const json_t* wanted = nullptr;
        size_t i;
        const json_t* item;
        json_array_foreach(list, i, item) {
            const json_t* text = json_object_get(item, "text");
            const json_t* code = json_object_get(item, "langue");
            if (!json_is_string(text) || !json_is_string(code)) continue;
            if (!first) first = item;
            const std::string c = json_string_value(code);
            if (c == lang && !wanted) wanted = item;
            if (c == "en" && !en) en = item;
        }
        const json_t* pick = wanted ? wanted : en ? en : first;
        if (pick) {
            r.synopsis = json_string_value(json_object_get(pick, "text"));
            r.lang = json_string_value(json_object_get(pick, "langue"));
        }
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
