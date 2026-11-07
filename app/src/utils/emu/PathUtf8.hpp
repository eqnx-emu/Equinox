#pragma once

#include <filesystem>
#include <string>

namespace emu {

// C++17 returns std::string from u8string(), C++20 returns std::u8string.
// These keep UTF-8 bytes intact in both.
inline std::string toUtf8(const std::filesystem::path& p) {
    auto s = p.u8string();
    return std::string(s.begin(), s.end());
}

inline std::filesystem::path fromUtf8(const std::string& s) {
    return std::filesystem::path(std::u8string(s.begin(), s.end()));
}

}  // namespace emu
