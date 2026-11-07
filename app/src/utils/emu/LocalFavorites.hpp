#pragma once

#include <string>
#include <vector>

struct json_t;

namespace emu {

// A console game marked as favorite. romFile is the file name in the
// console's ROM folder; title is what the shelf showed when it was starred.
struct LocalFavorite {
    std::string console, romFile, title;
};

// Reads "local_favorites": [{"console":"snes","rom":"...","title":"..."}].
// Never fails: a missing or non-array value gives an empty list, and an
// entry without a console or a plain file name, or one already listed, is
// skipped. A missing title becomes the file name without its extension.
std::vector<LocalFavorite> parseLocalFavorites(const json_t* array);

// The array to store under "local_favorites"; nullptr if jansson fails.
json_t* localFavoritesJson(const std::vector<LocalFavorite>& favorites);

bool isLocalFavorite(const std::vector<LocalFavorite>& favorites, const std::string& console,
                     const std::string& romFile);

// Adds the game at the end, or removes it when it is already there.
// Returns true when the game is a favorite afterwards.
bool toggleLocalFavorite(std::vector<LocalFavorite>& favorites, const LocalFavorite& favorite);

}  // namespace emu
