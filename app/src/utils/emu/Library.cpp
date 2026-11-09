#include "emu/Library.hpp"

#include <algorithm>
#include <cstdint>

namespace emu {

namespace {

// U+00C0..U+00FF; the multiplication and division signs have no fold and stay as they are.
const char* const kLatin1Fold[64] = {
    "a", "a", "a", "a", "a", "a", "ae", "c", "e", "e", "e", "e", "i", "i", "i", "i",
    "d", "n", "o", "o", "o", "o", "o", nullptr, "o", "u", "u", "u", "u", "y", "th", "ss",
    "a", "a", "a", "a", "a", "a", "ae", "c", "e", "e", "e", "e", "i", "i", "i", "i",
    "d", "n", "o", "o", "o", "o", "o", nullptr, "o", "u", "u", "u", "u", "y", "th", "y"};

std::string trim(const std::string& s) {
    size_t b = s.find_first_not_of(" \t");
    if (b == std::string::npos) return "";
    size_t e = s.find_last_not_of(" \t");
    return s.substr(b, e - b + 1);
}

// Name without a leading "the ", folded: the key the lists are sorted by.
std::string nameKey(const std::string& name) {
    std::string k = foldForSearch(name);
    if (k.compare(0, 4, "the ") == 0) k.erase(0, 4);
    return k;
}

bool knownRegion(const std::string& r) {
    static const char* const kRegions[] = {"USA",    "Europe",    "Japan",  "World",  "Brazil", "Korea", "Asia",
                                           "Australia", "Canada", "France", "Germany", "Spain",  "Italy"};
    return std::any_of(std::begin(kRegions), std::end(kRegions), [&](const char* k) { return r == k; });
}

}  // namespace

std::string displayName(const std::string& canonicalOrTitle) {
    std::string s = trim(canonicalOrTitle);
    while (!s.empty() && (s.back() == ')' || s.back() == ']')) {
        const char open = s.back() == ')' ? '(' : '[';
        size_t p = s.rfind(open);
        if (p == std::string::npos) break;
        s = trim(s.substr(0, p));
    }
    static const std::string kThe = ", The";
    size_t at = std::string::npos;
    if (s.size() >= kThe.size() && s.compare(s.size() - kThe.size(), kThe.size(), kThe) == 0) {
        at = s.size() - kThe.size();
    } else {
        size_t p = s.find(", The - ");
        if (p != std::string::npos) at = p;
    }
    if (at != std::string::npos) s = "The " + s.substr(0, at) + s.substr(at + kThe.size());
    return s;
}

std::string foldForSearch(const std::string& utf8) {
    std::string out;
    out.reserve(utf8.size());
    for (size_t i = 0; i < utf8.size(); ++i) {
        const auto c = static_cast<unsigned char>(utf8[i]);
        if (c >= 'A' && c <= 'Z') {
            out += static_cast<char>(c + 32);
        } else if ((c == 0xC3) && i + 1 < utf8.size() && (static_cast<unsigned char>(utf8[i + 1]) & 0xC0) == 0x80) {
            // C3 80..BF is U+00C0..U+00FF
            const char* f = kLatin1Fold[static_cast<unsigned char>(utf8[i + 1]) - 0x80];
            if (f) {
                out += f;
            } else {
                out += utf8[i];
                out += utf8[i + 1];
            }
            ++i;
        } else {
            out += static_cast<char>(c);
        }
    }
    return out;
}

std::vector<size_t> libraryOrder(const std::vector<LibraryItem>& items, const std::string& query, LibrarySort sort) {
    const std::string q = foldForSearch(trim(query));
    std::vector<size_t> idx;
    for (size_t i = 0; i < items.size(); ++i)
        if (q.empty() || foldForSearch(items[i].name).find(q) != std::string::npos) idx.push_back(i);

    // Worked out once: a sort compares each game many times.
    std::vector<std::string> names(items.size()), genres(items.size());
    for (size_t i : idx) {
        names[i] = nameKey(items[i].name);
        if (sort == LibrarySort::Genre) genres[i] = foldForSearch(items[i].genre);
    }
    auto byName = [&](size_t a, size_t b) { return names[a] < names[b]; };
    std::stable_sort(idx.begin(), idx.end(), [&](size_t a, size_t b) {
        switch (sort) {
            case LibrarySort::NameAsc:
                return byName(a, b);
            case LibrarySort::NameDesc:
                return byName(b, a);
            case LibrarySort::YearAsc:
            case LibrarySort::YearDesc: {
                const int ya = items[a].year, yb = items[b].year;
                if ((ya == 0) != (yb == 0)) return yb == 0;
                if (ya != yb) return sort == LibrarySort::YearAsc ? ya < yb : ya > yb;
                return byName(a, b);
            }
            case LibrarySort::Genre: {
                const bool ea = items[a].genre.empty(), eb = items[b].genre.empty();
                if (ea != eb) return eb;
                if (genres[a] != genres[b]) return genres[a] < genres[b];
                return byName(a, b);
            }
        }
        return false;
    });
    return idx;
}

std::vector<std::string> noIntroRegions(const std::string& canonical) {
    std::vector<std::string> out;
    size_t open = canonical.find('(');
    if (open == std::string::npos) return out;
    size_t close = canonical.find(')', open);
    if (close == std::string::npos) return out;
    const std::string body = canonical.substr(open + 1, close - open - 1);
    size_t pos = 0;
    while (true) {
        size_t comma = body.find(", ", pos);
        std::string part = body.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
        if (!knownRegion(part)) return {};
        out.push_back(part);
        if (comma == std::string::npos) break;
        pos = comma + 2;
    }
    return out;
}

}  // namespace emu
