#include "emu/LocalFavorites.hpp"

#include <jansson.h>

#include <algorithm>

namespace emu {

namespace {

std::string stringField(const json_t* object, const char* key) {
    const json_t* value = json_object_get(object, key);
    if (!value || !json_is_string(value))
        return "";
    return json_string_value(value);
}

// The ROM is opened as <roms>/<romFile>: a name, or folders and a name joined
// by '/', that cannot climb out of the console's folder.
bool plainFileName(const std::string& name) {
    if (name.empty() || name.find('\\') != std::string::npos) return false;
    size_t start = 0;
    while (true) {
        const size_t slash = name.find('/', start);
        const size_t len = slash == std::string::npos ? std::string::npos : slash - start;
        const std::string part = name.substr(start, len);
        if (part.empty() || part == "." || part == "..") return false;
        if (slash == std::string::npos) return true;
        start = slash + 1;
    }
}

bool sameGame(const LocalFavorite& f, const std::string& console, const std::string& romFile) {
    return f.console == console && f.romFile == romFile;
}

}  // namespace

std::vector<LocalFavorite> parseLocalFavorites(const json_t* array) {
    std::vector<LocalFavorite> out;
    if (!array || !json_is_array(array))
        return out;
    const size_t size = json_array_size(array);
    for (size_t i = 0; i < size; i++) {
        const json_t* item = json_array_get(array, i);
        if (!item || !json_is_object(item))
            continue;
        LocalFavorite favorite;
        favorite.console = stringField(item, "console");
        favorite.romFile = stringField(item, "rom");
        favorite.title = stringField(item, "title");
        if (favorite.console.empty() || !plainFileName(favorite.romFile))
            continue;
        if (isLocalFavorite(out, favorite.console, favorite.romFile))
            continue;
        if (favorite.title.empty())
            favorite.title = favorite.romFile.substr(0, favorite.romFile.rfind('.'));
        if (favorite.title.empty())
            favorite.title = favorite.romFile;
        out.push_back(std::move(favorite));
    }
    return out;
}

json_t* localFavoritesJson(const std::vector<LocalFavorite>& favorites) {
    json_t* array = json_array();
    if (!array)
        return nullptr;
    for (const LocalFavorite& favorite : favorites) {
        json_t* item = json_object();
        if (!item)
            continue;
        json_object_set_new(item, "console", json_string(favorite.console.c_str()));
        json_object_set_new(item, "rom", json_string(favorite.romFile.c_str()));
        json_object_set_new(item, "title", json_string(favorite.title.c_str()));
        json_array_append_new(array, item);
    }
    return array;
}

bool isLocalFavorite(const std::vector<LocalFavorite>& favorites, const std::string& console,
                     const std::string& romFile) {
    return std::any_of(favorites.begin(), favorites.end(),
                       [&](const LocalFavorite& f) { return sameGame(f, console, romFile); });
}

bool toggleLocalFavorite(std::vector<LocalFavorite>& favorites, const LocalFavorite& favorite) {
    auto it = std::find_if(favorites.begin(), favorites.end(), [&](const LocalFavorite& f) {
        return sameGame(f, favorite.console, favorite.romFile);
    });
    if (it != favorites.end()) {
        favorites.erase(it);
        return false;
    }
    if (favorite.console.empty() || !plainFileName(favorite.romFile))
        return false;
    favorites.push_back(favorite);
    return true;
}

}  // namespace emu
