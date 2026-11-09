// Reads .7z archives held in memory, through the LZMA SDK decoder.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace sevenz {

enum class Status { Ok, Damaged, NoMatch, TooBig };

struct Found {
    std::string name;
    uint64_t size = 0;
    bool hasCrc = false;
    uint32_t crc = 0;
    std::vector<uint8_t> bytes;  // only when asked to extract
};

// The first file, in archive order, whose name `wanted` accepts. maxBlock limits
// what must be unpacked to reach it: in a solid archive that is more than the
// file itself. Without extract nothing is unpacked but the archive's directory.
Status firstMatching(const uint8_t* archive, size_t size,
                     const std::function<bool(const std::string&)>& wanted, uint64_t maxFile,
                     uint64_t maxBlock, bool extract, Found& out);

}  // namespace sevenz
