#pragma once

#include <string>
#include <vector>

#include "emu/Console.hpp"

namespace emu {

struct RomEntry {
    std::string path, fileName, title;  // title = file name without extension
};

// Never throws. Regular, non-empty, non-hidden files directly in `dir`
// whose extension the console accepts, sorted case-insensitively.
std::vector<RomEntry> scanRoms(const std::string& dir, const Console& console);

}  // namespace emu
