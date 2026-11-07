#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace emu {

// Drops trailing (...) and [...] groups and moves a trailing ", The" in front: "X, The - Y" -> "The X - Y".
std::string displayName(const std::string& canonicalOrTitle);
// ASCII lower case, and Latin-1 accented letters (U+00C0..U+00FF) without the accent.
std::string foldForSearch(const std::string& utf8);

enum class LibrarySort { NameAsc = 0, NameDesc, YearAsc, YearDesc, Genre };
struct LibraryItem {
    std::string name;
    int year = 0;
    std::string genre;
};
// Indexes of the items that match the query, in the requested order. Year 0 and empty genre go last.
std::vector<size_t> libraryOrder(const std::vector<LibraryItem>& items, const std::string& query, LibrarySort sort);

// Regions in the first (...) group of a No-Intro name; empty when that group is not a list of known regions.
std::vector<std::string> noIntroRegions(const std::string& canonical);

}  // namespace emu
