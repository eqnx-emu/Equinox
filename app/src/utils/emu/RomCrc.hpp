#pragma once

#include <cstdint>
#include <string>

#include "emu/Console.hpp"

namespace emu {

// CRC32 of a ROM as the libretro database lists it: without the 512-byte
// copier header some SNES dumps carry. Reads .zip by its first ROM entry.
// Never throws; on failure returns false with a message in `error`.
bool romCrc32(const std::string& path, const Console& console, uint32_t& crc, std::string& error);

}  // namespace emu
