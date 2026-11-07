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
};
// The synopsis comes in `lang`, else English, else the first one available.
SsResult ssParse(long httpStatus, const std::string& body, const std::string& lang);

}  // namespace emu
