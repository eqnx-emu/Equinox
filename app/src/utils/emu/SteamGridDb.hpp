#pragma once

#include <string>

// SteamGridDB (steamgriddb.com): covers for the games libretro has none of.
// Every API call carries the user's own key; the image itself comes from a
// CDN without it.

namespace emu {

enum class SgdbStatus { Ok, NotFound, BadKey, Busy };

// "Authorization: Bearer <key>", for the API calls only.
std::string sgdbAuthHeader(const std::string& key);

// The name to look for: the libretro name without its "(USA)", "[!]" and
// such, else the file's title the same way.
std::string sgdbSearchTerm(const std::string& canonical, const std::string& title);
std::string sgdbSearchUrl(const std::string& term);
std::string sgdbGridsUrl(int gameId);

struct SgdbGame {
    SgdbStatus status = SgdbStatus::Busy;
    int id = 0;
};
// The game whose name matches the term, letters and digits only, ignoring
// case; a name that only contains the term, or the other way round, when
// none matches whole. NotFound rather than a guess.
SgdbGame sgdbParseSearch(long httpStatus, const std::string& body, const std::string& term);

struct SgdbGrid {
    SgdbStatus status = SgdbStatus::Busy;
    std::string url;
};
// A PNG or JPEG grid, a wide one first (the cover fills a wide label).
SgdbGrid sgdbParseGrids(long httpStatus, const std::string& body);

}  // namespace emu
