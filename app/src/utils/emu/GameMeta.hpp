#pragma once

#include <cstdint>
#include <string>

#include "emu/Console.hpp"

struct json_t;

namespace emu {

enum class PartState { Unknown, Ok, Missing, Skipped };

struct GameMeta {
    uint64_t size = 0;
    int64_t mtime = 0;  // of the ROM when the data was downloaded
    bool identified = false;
    uint32_t crc = 0;
    std::string canonical;
    PartState snap = PartState::Unknown, title = PartState::Unknown, synopsis = PartState::Unknown;
    std::string synopsisText, synopsisLang;
    // From ScreenScraper; libretro-database's own facts come first.
    std::string genre, developer, publisher;
    int players = 0, year = 0;
};

std::string metaPath(const ConsolePaths& paths, const std::string& title);  // <meta>/<title>.json

// False when the file is missing, unreadable or its "v" is not 1.
bool loadMeta(const std::string& path, GameMeta& out);
// Writes <path>.part, then swaps it in with replaceFile.
bool saveMeta(const std::string& path, const GameMeta& meta);

// The fields of a meta as JSON, for files that carry many of them. Owned by the caller.
json_t* metaToJson(const GameMeta& meta);
bool metaFromJson(const json_t* object, GameMeta& out);

bool romStat(const std::string& romPath, uint64_t& size, int64_t& mtime);
// The data still describes the ROM that is on the card.
bool metaFresh(const GameMeta& meta, uint64_t size, int64_t mtime);

}  // namespace emu
