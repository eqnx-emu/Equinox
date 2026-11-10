#include "emu/SteamGridDb.hpp"

#include <jansson.h>

#include <algorithm>
#include <cctype>

#include "emu/CoverName.hpp"

namespace emu {

namespace {

constexpr const char* kApi = "https://www.steamgriddb.com/api/v2";

std::string text(const json_t* object, const char* key) {
    const json_t* value = json_object_get(object, key);
    return json_is_string(value) ? json_string_value(value) : std::string();
}

// Everything before the first " (" or " [".
std::string withoutTags(const std::string& name) {
    size_t cut = name.size();
    for (const char* tag : {" (", " ["}) cut = std::min(cut, name.find(tag));
    std::string out = name.substr(0, cut);
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

std::string folded(const std::string& s) {
    std::string out;
    for (unsigned char c : s)
        if (std::isalnum(c)) out += static_cast<char>(std::tolower(c));
    return out;
}

SgdbStatus statusOf(long httpStatus) {
    if (httpStatus == 401 || httpStatus == 403) return SgdbStatus::BadKey;
    if (httpStatus == 404) return SgdbStatus::NotFound;
    return SgdbStatus::Busy;  // no answer, 429, 5xx
}

// The "data" array of a successful answer, or null. Owned by `root`.
const json_t* dataOf(const json_t* root) {
    if (!json_is_true(json_object_get(root, "success"))) return nullptr;
    const json_t* data = json_object_get(root, "data");
    return json_is_array(data) ? data : nullptr;
}

}  // namespace

std::string sgdbAuthHeader(const std::string& key) { return "Authorization: Bearer " + key; }

std::string sgdbSearchTerm(const std::string& canonical, const std::string& title) {
    std::string term = withoutTags(canonical);
    if (term.empty()) term = withoutTags(title);
    return term;
}

std::string sgdbSearchUrl(const std::string& term) {
    return std::string(kApi) + "/search/autocomplete/" + urlEncode(term);
}

std::string sgdbGridsUrl(int gameId) {
    return std::string(kApi) + "/grids/game/" + std::to_string(gameId) +
           "?types=static&mimes=image/png,image/jpeg&nsfw=false&humor=false";
}

SgdbGame sgdbParseSearch(long httpStatus, const std::string& body, const std::string& term) {
    SgdbGame out;
    if (httpStatus != 200) {
        out.status = statusOf(httpStatus);
        return out;
    }
    json_error_t err;
    json_t* root = json_loadb(body.data(), body.size(), 0, &err);
    if (!root) return out;
    const json_t* data = dataOf(root);
    if (data) {
        const std::string want = folded(term);
        int whole = 0, partial = 0;
        size_t i;
        const json_t* item;
        json_array_foreach(data, i, item) {
            const json_t* id = json_object_get(item, "id");
            if (!json_is_integer(id) || want.empty()) continue;
            const std::string name = folded(text(item, "name"));
            if (name.empty()) continue;
            if (name == want) {
                whole = static_cast<int>(json_integer_value(id));
                break;
            }
            if (!partial && (name.find(want) != std::string::npos || want.find(name) != std::string::npos))
                partial = static_cast<int>(json_integer_value(id));
        }
        out.id = whole ? whole : partial;
        out.status = out.id ? SgdbStatus::Ok : SgdbStatus::NotFound;
    }
    json_decref(root);
    return out;
}

SgdbGrid sgdbParseGrids(long httpStatus, const std::string& body) {
    SgdbGrid out;
    if (httpStatus != 200) {
        out.status = statusOf(httpStatus);
        return out;
    }
    json_error_t err;
    json_t* root = json_loadb(body.data(), body.size(), 0, &err);
    if (!root) return out;
    if (const json_t* data = dataOf(root)) {
        std::string tall;
        size_t i;
        const json_t* item;
        json_array_foreach(data, i, item) {
            const std::string mime = text(item, "mime");
            const std::string url = text(item, "url");
            if ((mime != "image/png" && mime != "image/jpeg") || url.rfind("https://", 0) != 0)
                continue;
            const json_t* w = json_object_get(item, "width");
            const json_t* h = json_object_get(item, "height");
            const bool wide = json_is_integer(w) && json_is_integer(h) &&
                              json_integer_value(w) >= json_integer_value(h);
            if (wide) {
                out.url = url;
                break;
            }
            if (tall.empty()) tall = url;
        }
        if (out.url.empty()) out.url = tall;
        out.status = out.url.empty() ? SgdbStatus::NotFound : SgdbStatus::Ok;
    }
    json_decref(root);
    return out;
}

}  // namespace emu
