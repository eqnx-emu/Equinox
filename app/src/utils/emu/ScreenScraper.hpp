#pragma once

#include <cstdint>
#include <string>

#include "emu/Console.hpp"

namespace emu {

struct SsConfig {
    std::string devId, devPassword, softName, user, password, lang;
};
bool ssConfigured(const SsConfig& cfg);

struct SsQuery {
    int systemId;
    uint32_t crc;
    std::string romName;
    uint64_t romSize;
};
// ScreenScraper system id; 0 when the console is not supported.
int ssSystemId(const Console& console);
std::string ssUrl(const SsConfig& cfg, const SsQuery& query);

enum class SsStatus { Ok, NotFound, BadLogin, Quota, Transient };
struct SsResult {
    SsStatus status = SsStatus::Transient;
    std::string synopsis, lang;
    std::string genre, developer, publisher;
    int players = 0;  // the most at once ("1-2" is 2)
    int year = 0;     // the earliest release
    // PNG or JPEG media; empty when ScreenScraper has none.
    std::string snapUrl, titleUrl, boxUrl;
};
// The synopsis and the genre come in `lang`, else English, else the first
// one available. Media of the whole world come first, then USA, Europe, Japan.
SsResult ssParse(long httpStatus, const std::string& body, const std::string& lang);

}  // namespace emu
