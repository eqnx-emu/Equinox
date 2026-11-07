#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "emu/Console.hpp"

namespace emu {

struct GameInfo {
    uint32_t crc = 0;
    std::string name, genre, developer, publisher, franchise;
    int players = 0, year = 0;
};

class GameDb {
  public:
    // False when the header is not "#equinox-gamedb<TAB>1"; the db is then empty.
    bool loadText(const std::string& text);
    bool loadFile(const std::string& path);
    const GameInfo* find(uint32_t crc) const;
    size_t size() const { return games_.size(); }

  private:
    std::vector<GameInfo> games_;  // sorted by crc
};

// Bundled database of a console; empty outside the app build.
std::string gameDbPath(const Console& console);

}  // namespace emu
